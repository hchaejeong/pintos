/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include <hash.h>
#include "threads/mmu.h"

struct list frame_list;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_list); // 언제나 새로 선언하면 init 해줘야지~
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);
unsigned spt_hash (const struct hash_elem *elem, void *aux UNUSED);
bool spt_compare (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
//커널이 새로운 페이지 request을 받을면 호출이 된다
//type parameter에 맞는 페이지를 초기화한 뒤 다시 유저 프로그램으로 제어권을 넘긴다
//즉 실제로 이 페이지가 어떤 프레임과 연결되어 있지는 않은 상태이다
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		//일단 type에 맞춘 uninitialized page를 하나 생성해주고 나중에 page fault가 발생하면 이 페이지에 주어진 vm_type에 맞게 초기화 될것
		//지금 spt에 해당 페이지 (upage)가 없어야 새롭개 만들어주니까 이 if statement안에서 수행해야한다
		//일단 allocate a new page니까 공간을 할당해줘야한다
		struct page *new_page = malloc(sizeof(struct page));
		//page type에 따라 appropriate initializer을 세팅해줘야한다
		if (VM_TYPE(type) == VM_ANON) {
			uninit_new(new_page, upage, init, type, aux, anon_initializer);
		} else if (VM_TYPE(type) == VM_FILE) {
			uninit_new(new_page, upage, init, type, aux, file_backed_initializer);
		} 

		//유저한테 다시 control을 준다
		//writable을 인자로 받았으니 이 속성을 페이지에 업데이트 시켜줘야한다
		new_page->write = writable;
		/* TODO: Insert the page into the spt. */
		spt_insert_page(spt, new_page);
		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function. */
	//spt에서 해당 va를 가지고 있는 페이지를 빼오는거기 때문에 hash search 함수들을 이용하면 될거같다
	page->va = pg_round_down(va);

	struct hash_elem *elem;
	elem = hash_find(&(spt->page_table), &page->hash_elem);
	if (elem == NULL) {
		return NULL; 
	}

	page = hash_entry(elem, struct page, hash_elem);
	return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */
	struct hash_elem *elem = hash_insert(&(spt->page_table), &(page->hash_elem));
	if (elem == NULL) {		//해쉬테이블에 같은 addr를 가진 페이지가 존재하고 있지 않다는거기 때문에 insert할 수 있다
		succ = true;
	}

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	//struct frame *frame = NULL;
	struct frame *frame;
	/* TODO: Fill this function. */
	// gitbook을 보면 이걸 제일 먼저 채우라고 적혀있음
	// 위 설명을 읽어보면, 
	/* 1. user pool로부터 새로운 physical page를 얻는다 (palloc_get_page 이용) */
	frame = (struct frame*)malloc(sizeof(struct frame)); // 먼저 크기 배정
	frame->kva = palloc_get_page(PAL_USER); // user pool로부터 새로운 page 얻음
	// 여기서, 만약 이미 빈 페이지였다면 그냥 그대로 return하면 되는데,
	// 이미 frame이 다 차 있어서 빈 페이지가 더이상 남아있지 않다면 victim을 쫓아내고 빈칸으로 만들어야 함
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);

	if (frame->kva != NULL) {
		// 이미 빈 칸이 있어서 그게 배치된 경우
		// 새로운 frame을 가져왔으니 page를 초기화해주고, frame list에 넣기
		frame->page = NULL;
		list_push_back(&frame_list, &frame->elem);
	} else {
		// 빈칸이 없어서 배당이 안 된 경우
		// victim을 evict하고, 새로운 frame으로 비운다
		// 위에 보니 vm_evict_frame()이라는 함수를 사용해야 하는 것 같음
		// 근데 gitbook에는 일단 panic("todo")를 사용하라고 적혀있는 것 같은데...
		PANIC("todo");
	}
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */


	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	//struct page *page = NULL;
	struct page *page;
	/* TODO: Fill this function */
	// 얘는 page에 va를 할당하라는 claim을 거는 함수
	// gitbook 말로는 먼저 page를 get한 다음,
	// vm_do_claim_page를 하랜다.
	page = spt_find_page(&thread_current()->spt, va);
	if (page != NULL) {
		return vm_do_claim_page (page);
	} else {
		return false;
	}

}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// gitbook 설명대로라면, 위에서 vm_get_page로 frame을 갖고 온 다음에,
	// MMU를 setting해준다. 즉, page table 안에서 va와 pa를 mapping하는걸 추가한다.
	// 그리고, 잘 완료되었으면 true, 아니면 false를 반환한다
	//bool mapping_va_pa = install_page(page->va, frame->kva, page->write);
	bool mapping_va_pa = pml4_set_page(&thread_current()->pml4, page->va, frame->kva, page->write);
	if (mapping_va_pa) {
		return swap_in (page, frame->kva);
	} else {
		return false;
	}

	//return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	//그냥 해쉬테이블을 init해주면된다
	hash_init(&(spt->page_table), spt_hash, spt_compare, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}

//spt를 해쉬테이블 구조로 쓰기 때문에 페이지 입력할때 어떻게 넣을지에 대한 해쉬 함수를 적어줘야한다
//pintos가 쓰라는 방식으로 address를 key로 설정해서 해쉬한다
unsigned
spt_hash (const struct hash_elem *elem, void *aux UNUSED) {
	const struct page *p = hash_entry(elem, struct page, hash_elem);
	return hash_bytes(&p -> va, sizeof p->va);
}

//해쉬 테이블에서 각 key들을 비교해서 같은 key로 어떤 element가 들어오지 않도록 해야한다
//즉, 해쉬 테이블의 각 element를 비교하는 함수를 만들어줘야한다
bool
spt_compare (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
	const struct page *pa = hash_entry(a, struct page, hash_elem);
	const struct page *pb = hash_entry(b, struct page, hash_elem);

	return pa->va < pb->va;
}