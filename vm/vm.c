/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
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

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
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

		/* TODO: Insert the page into the spt. */
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function. */

	return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */

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
	//int mapping_va_pa = install_page(page->va, frame->kva, page->writable);
	//int mapping_va_pa = install_page(page->va, frame->kva, is_writable(page));
	int mapping_va_pa = pml4_set_page(page->va, frame->kva, is_writable(page));
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
