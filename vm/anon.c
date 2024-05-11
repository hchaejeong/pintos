/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "bitmap.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* swap을 위한 table이 필요함 - bitmap을 이용해야 */
struct bitmap *swap_list;

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	//swap_disk = NULL;

	/* bitmap을 사용하여 init해주면 됨 */
	swap_disk = disk_get(1, 1);
	swap_list = bitmap_create(disk_size(swap_disk) / 8); // 하드드라이브 최소 기억 단위가 8바이트임!
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page->bitmap_index = -1; // index는 뭐든지 0부터 시작하므로, 0인 순간 이미 bitmap에 존재한다는 의미가 됨. 아직 bitmap에 들어가지 않은 상태의 init은 -1로 해줘야
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;

	
	//printf("swap in이 문제야?\n");
	// swap_in 할때 page가 없다는 오류가 생긴다.
	//printf(" swap in index: %d\n", anon_page->bitmap_index);
	// swap disk에 이 page의 index가 없으면 fail,
	// disk에 있으면 읽은 다음에, swap in되었다는 뜻인 false으로 세팅
	bool is_there = bitmap_test(swap_list, anon_page->bitmap_index);
	if (is_there) {
		// disk에 있는 data들을 kva로 읽어오면 됨
		for (int i=0; i<8; i++) {
			disk_read(swap_disk, (anon_page->bitmap_index)*8+i, page->frame->kva+DISK_SECTOR_SIZE*i);
		}
		// 그리고 이제 swap in 되었다는 0으로 세팅
		bitmap_set(swap_list, anon_page->bitmap_index, false);
		//printf("page addr: 0x%x, kva: 0x%x\n", page, kva);
		//printf("kernel에 있냐 %d\n", is_kernel_vaddr(page));
		page->frame->kva = kva;
		//printf("(swap in) page addr: 0x%x, frame addr: 0x%x, kva: 0x%x\n", page, page->frame, page->frame->kva);
		return true;
	} else {
		//printf("설마 is_there이 이상해?\n");
		return false;
	}
	
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	
	//printf("swap out이 문제야?\n");

	// swap in과 반대다. page의 내용을 disk에 적어서 백업해두고, page는 삭제하는 용도
	// 그럼 먼저 disk에 넣을 수 있는 공간을 찾아준다.
	// false라는 건 swap in 되었어서 이미 page에 다시 적혀서 disk에서는 없어져도 되는 애들임
	size_t empty_index = bitmap_scan(swap_list, 0, 1, false);
	if (empty_index == BITMAP_ERROR) {
		return false; // BITMAP_ERROR라는건 거기 안에 빈공간이 없다는 거다...
	} else {
		// index가 잘 찾아졌으면, swap in과 반대로 disk write를 해주면 됨
		for (int i=0; i<8; i++) {
			disk_write (swap_disk, empty_index*8+i, page->frame->kva+DISK_SECTOR_SIZE*i);
		}
		// 그리고 이제 swap out 되었다는 1로 세팅
		bitmap_set(swap_list, empty_index, true);
		// page의 내용들을 disk로 다 옮겼으므로 이제 page를 reset해줘야함
		pml4_clear_page(thread_current()->pml4, page->va);
		anon_page->bitmap_index = empty_index;
		//printf("swap out index: %d\n", anon_page->bitmap_index);
		page->frame = NULL;

		return true;
	}
	
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	//printf("anon destroy가 문제야?\n");
	/*
	//이 페이지가 보유한 리소스를 해제하는거기 때문에 free까지는 할 필요 없다
	struct segment_info *aux = anon_page->aux;
	if (aux) {
		//file_close(aux->page_file);
		free(aux);
	}
	//페이지랑 물리 프레임의 할당도 free해줘야한다

	*/
	if (page->frame != NULL) {
		list_remove(&(page->frame->elem));
		free(page->frame);
		page->frame = NULL;
	}
}
