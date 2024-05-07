/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"

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
	swap_list = bitmap_create(disk_size(swap_disk) / 8);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	/*
	//이 페이지가 보유한 리소스를 해제하는거기 때문에 free까지는 할 필요 없다
	struct segment_info *aux = anon_page->aux;
	if (aux) {
		file_close(aux->page_file);
		free(aux);
	}
	//페이지랑 물리 프레임의 할당도 free해줘야한다
	if (page->frame != NULL) {
		list_remove(&(page->frame->elem));
		free(page->frame);
		page->frame = NULL;
	}
	*/
}
