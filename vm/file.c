/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "threads/mmu.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

	/* 처음 init */
	struct segment_info *info = page->uninit.aux;
	file_page->aux = info;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	//struct file_page *file_page UNUSED = &page->file;
	struct file_page *file_page = &page->file;

	/* swap in 하는 것도 aux의 각 속성에 그대로 대입해주기만 하면 됨 */
	struct segment_info *info = (struct segment_info*)malloc(sizeof(struct segment_info));
	info->page_file = file_page->aux->page_file;
	info->offset = file_page->aux->offset;
	info->read_bytes = file_page->aux->read_bytes;
	info->zero_bytes = file_page->aux->zero_bytes;

	//return file lazy load
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	//struct file_page *file_page UNUSED = &page->file;
	struct file_page *file_page = &page->file;

	/* 이번에는 file이 적혀있는 page를 찾아서, page에 적혀있는 내용들을 file에 다시 적고
	page를 clear 해주면 됨 */
	if (pml4_is_dirty(thread_current()->pml4, page->va)) {
		/* 해당 page가 dirty한 상태 (file 내용이 적혀있는 상태)면 page에 file내용 적어준다 */
		file_write_at(file_page->aux->page_file, page->va, file_page->aux->read_bytes ,file_page->aux->offset);
		pml4_set_dirty(thread_current()->pml4, page->va, 0);
	}
	/* page에 아무것도 안 적혀있으면 당연히 file에 writeback 안 하지! */

	/* 이제 page에 있는 정보들을 file로 writeback한거니까, 이제 page를 비워줘야함 */
	pml4_clear_page(thread_current()->pml4, page->va); // page 완전히 clear 해주고,
	page->frame = NULL; // frame은 NULL한 상태로 초기화 해줘야 함

}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	//struct file_page *file_page UNUSED = &page->file;
	struct file_page *file_page = &page->file;
	/* 거실에서 코드 적고 있는데 아빠가 창문을 활짝 열어두었다.
	빗소리가 참 좋다. 뭔가 이렇게 온전히 빗소리만을 들어본 적이 언제적인지 모르겠다.
	한동안 후덥지근 했는데도 이렇게 세차게 비가 내리니 선선한 바람이 불어온다.
	꽃가루와 미세먼지 걱정 없는 시원한 공기. 창문을 열었을 뿐인데 기분이 좋아진다.
	힘내서 코드 마무리 해야지...! */

	/* 이번에는 writeback하는 걸로 끝나는게 아니다. 완전히 page를 destroy해야한다! */
	if (pml4_is_dirty(thread_current()->pml4, page->va)) {
		file_write_at(file_page->aux->page_file, page->va, file_page->aux->read_bytes, file_page->aux->offset);
	} // 여기까지는 같다. writeback까지는 해야하니까!

	/* hash table에서 삭제해줘야 한다. */
	hash_delete(&thread_current()->spt.page_table, &page->hash_elem);

	/* 그리고 page를 free해줘야 한다. */
	if (page->frame != NULL) {
		free(page->frame);
	}
	page->frame = NULL;

}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	/* mmap 파트 gitbook 설명. anon pages와는 다르게, mmap는 file-backed mapping임.
	이 page 안의 contents들은 some existing file의 data들을 mirror 함.
	mmap page들이 unmap되거나 swap out되면, 모든 변경사항은 file에 반영됨. */

	/* 우리 VM system은 mmap region으로 page를 lazily하게 load해야함.
	그리고, mmaped file 자체를 mapping을 위한 backing store로 사용해야함. */

	/* 그냥 mmap 함수에 대한 설명:
	length bytes를 map하는데, addr 위치의 process의 va space에 함.
	전체 file은 addr부터 시작하는 연속적인 virtual page에 map됨.
	length가 PGSIZE의 배수가 아니면, 몇 byte는 EOF 넘어서 약간 "stick out"됨.
	page fault 할때는, 이렇게 튀어나온 byte들을 zero로 set하고,
	page가 disk로 written back했을 때 삭제한다.
	만약 성공하면, file이 map된 va를 return한다.
	실패하면, NULL을 반환한다. */

	/* page마다 read byte가 있고, zero byte가 있다. 즉, stick out되는 부분들을 다 zero byte로 퉁치는건가?
	즉, zero byte는 전체 page 길이에서 read byte를 뺀 부분. */
	/* addr부터 시작해서, page단위로 file의 정보들을 잘라서 저장하면 됨. */

	/* 파일은 어떻게 하던 간에 reopen을 사용해야 한다 (syscall-ummap 함수에 정리해둠) */
	struct file *file = file_reopen(file);

	if (file == NULL) {
		/* 당연히 reopen된 file이 NULL이면 return. */
		return NULL;
	}
	/* 그럼 이제 file이 reopen 되었으니까, 전체 read byte와 zero byte 구해보면, */
	size_t whole_read_len;
	size_t whole_zero_len;
	if (file_length(file) > length) {
		/* 실제 file의 길이가 더 길면, length만큼 잘라야 하는 것. 따라서 read byte는 length (이 이상 더 읽으면 안됨) */
		whole_read_len = length;
	} else {
		/* length가 더 길면, 그만큼 좀 넉넉?하게 do_mmap을 실행시킨 것이므로 우리는 그냥
		file_length만큼만 읽으면 됨. 굳이 더 읽을 필요가 없으니까 */
		whole_read_len = file_length(file);
	}
	/* zero len은 page 단위 안에 read byte들이 채워지고, 이제 남은 공간 부분임
	예를 들어, whole_read_len이 대충 2.5개의 page를 쓴다면 whole_zero_len은 0.5가 되는 것 */
	whole_zero_len = pg_round_up(whole_read_len) - whole_read_len;

	/* return할 원래 addr 저장 */
	void *return_addr = addr;
	/* 각 page별 addr이 될 변수 만들기 */
	void *page_addr = addr;

	/* 이제부터는 전체 read 부분들을 읽어나가기 시작 */
	while (whole_read_len + whole_zero_len > 0) {
		/* 여기서부터는 page 단위로 끊어서 읽게 됨. 따라서 page 단위의 read/zero byte 구하기 */
		size_t page_read_len;
		size_t page_zero_len; // 물론 page zero len은 마지막 page에만 있겠지만!
		
		if (whole_read_len > PGSIZE) {
			/* 이제 남은 읽을 부분이 page size보다 크다면, 또 page size만큼만 나눠서 읽으면 됨 */
			page_read_len = PGSIZE; // 그럼 이제 zero len은 0이 되므로 신경 ㄴㄴ

			/* 정보들을 구조체에 저장 */
			struct segment_info *info = (struct segment_info*)malloc(sizeof(struct segment_info));
			info->page_file = file;
			info->offset = offset;
			info->read_bytes = page_read_len;
			info->zero_bytes = 0;

			/* page object 만들자. syscall에서 적은 것처럼 함수 사용 */
			if (!vm_alloc_page_with_initializer(VM_FILE, page_addr, writable, lazy_load_segment, info)) {
				return NULL; // 제대로 page에 file이 안 쓰였으면 NULL 반환
			}

			/* 그리고 각 변수들 업데이트 */
			whole_read_len = whole_read_len - page_read_len;
			// zero len은 그대로
			page_addr = page_addr + PGSIZE; // page size만큼 addr 증가
			offset = offset + page_read_len; // 시작하는 부분 addr도 증가

		} else {
			/* 이제 남은 읽을 부분이 page size보다 작거나 같다면, 이제는 이부분만 읽고 끝~ */
			page_read_len = whole_read_len;
			page_zero_len = PGSIZE - page_read_len; // zero len 부분은 page에서 남은 부분

			/* 정보들을 구조체에 저장 - 이건 똑같음 */
			struct segment_info *info = (struct segment_info*)malloc(sizeof(struct segment_info));
			info->page_file = file;
			info->offset = offset;
			info->read_bytes = page_read_len;
			info->zero_bytes = page_zero_len; // 얘만 추가

			if (!vm_alloc_page_with_initializer(VM_FILE, page_addr, writable, lazy_load_segment, info)) {
				return NULL;
			}

			/* 각 변수들 업데이트 */
			whole_read_len = 0; // 다 읽음
			whole_zero_len = whole_zero_len - page_zero_len; // 혹시라도 zero len이 남아있을 수도 있으니까
			page_addr = page_addr + PGSIZE;
			offset = offset + page_read_len;
		}
	}
	
	return return_addr; // file mapping page의 시작 addr 반환!

}

/* Do the munmap */
void
do_munmap (void *addr) {
	/* munmap 함수 설명:
	addr 주변 특정 range부분의 mapping된 걸 unmmap하는 역할.
	page들이 다시 file에 쓰여지고, 안 쓰여진 페이지는 쓰이면 안 됨.
	unmap되므로, virtual page list에서 page가 제거되어야 함 */
	/* 모든 mapping은 exit이 발동되면 무조건 unmap됨.
	unmap될 때는, process가 썼던 모든 page들이 file로 written back되고, 안 쓰여진 애들은 written되면 안된다.
	그리고 page들은 process의 list of virtual pages에서 사라진다
	file을 close or remove하는건 unmap이랑은 아무 관련이 없음.
	각 mapping마다 seperate & independent한 ref를 쓰려면 file_reopen 함수를 쓰는 것임.
	2개 이상의 process가 같은 file에 map되어있을 때,
	각 process가 consistent datafmf 볼 수 있는 requirement는 없음. */

	/* 그렇다면, addr부터 시작해서 계속 page를 하나하나 넘겨가며 dirty한 경우에는
	page의 내용들을 다시 file에다가 적어주면 되고, 그게 아니면 그냥 넘어가면 됨
	swap out하는 것처럼 write 후 page clear 해줘야! */
	
	struct page* page = spt_find_page(&thread_current()->spt, addr);

	while (page != NULL) {
		/* 즉, page가 NULL이 아닐 때까지만 돌려주면 됨 */
		
		/* swap out과 같은 방법으로 진행 */
		struct segment_info *info = (struct segment_info *) page->uninit.aux;
		if (pml4_is_dirty(thread_current()->pml4, page->va)) {
			file_write_at(info->page_file, addr, info->read_bytes, info->offset);
			pml4_set_dirty(thread_current()->pml4, page->va, 0); // 이미 dirty한 거 다시 file에 writeback했으니 0으로 만들고 이제 초기화 해줘야
		}
		pml4_clear_page(thread_current()->pml4, page->va);
		addr = addr + PGSIZE;
	}

}
