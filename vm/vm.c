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
		/*
		struct page *new_page = malloc(sizeof(struct page));
		*/
		struct page *new_page = (struct page*)malloc(sizeof(struct page));
		//page type에 따라 appropriate initializer을 세팅해줘야한다
		if (new_page == NULL) {
			return false;
		}

		if (VM_TYPE(type) == VM_ANON) {
			uninit_new(new_page, upage, init, type, aux, anon_initializer);
		} else if (VM_TYPE(type) == VM_FILE) {
			uninit_new(new_page, upage, init, type, aux, file_backed_initializer);
		} 
		//uninit_new하기전에 정보를 넣는건 무의미, 아예 새로 할당되기 때문에 안에 있었던거는 다 날라간다

		//유저한테 다시 control을 준다
		//writable을 인자로 받았으니 이 속성을 페이지에 업데이트 시켜줘야한다
		new_page->write = writable;
		/* TODO: Insert the page into the spt. */
		/*
		spt_insert_page(spt, new_page);
		return true;
		*/
		return spt_insert_page(spt, new_page);
	} else {
		return false;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = (struct page *)malloc(sizeof(struct page));
	/* TODO: Fill this function. */
	//spt에서 해당 va를 가지고 있는 페이지를 빼오는거기 때문에 hash search 함수들을 이용하면 될거같다
	struct page p;
	p.va = pg_round_down(va);
	//page->va = pg_round_down(va);

	struct hash_elem *elem;
	/*
	elem = hash_find(&(spt->page_table), &(p.hash_elem));
	*/
	elem = hash_find(&thread_current()->spt.page_table, &p.hash_elem);
	if (elem == NULL) {
		return NULL; 
	}

	page = hash_entry(elem, struct page, hash_elem);
	return page;
	//return hash_entry(elem, struct page, hash_elem);
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
	if (hash_delete(&(spt->page_table), &(page->hash_elem)) == NULL) {
		return;
	}
	
	vm_dealloc_page (page);
	return;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	//printf("여기 들어가냐?\n");
	 /* TODO: The policy for eviction is up to you. */
	 // 여기서 policy는 up to u라고 하긴 하는데, 일단 알려준대로 해야겠지?
	 // 그러면, 1이라고 access bit이 표시 된 애는 최근에 참조된 애고
	 // 0인 애들은 최근에 참조가 안 된 애들이다. 그러면 이제 얘네들이 지워져도 되는것임!
	 // 그럼 일단, frame list를 쭉 돌아본다. 여기서 1인 애들은 0으로 바꿔주고,
	 // 0인 애들은 victim인 것이다.
	 /*
	 if (list_empty(list_begin(&frame_list))) {
		return;
	 }
	 */

	 if (list_empty(&frame_list)) {
		return;
	 }

	 //return list_entry(list_begin(&frame_list), struct frame, elem);

	 
	 struct list_elem *frame;

	 for (frame = list_begin(&frame_list); frame != list_end(&frame_list); frame = list_next(frame)) {
		victim = list_entry(frame, struct frame, elem);
		if (pml4_is_accessed(thread_current()->pml4, victim->page->va)) {
			// recently하게 방문되었으면 true를 반환해줌. 따라서, 이렇게 최근에 반환된 애들은 이제 0으로 세팅해주면 됨
			pml4_set_accessed(thread_current()->pml4, victim->page->va, false);
		} else {
			break; // 이러면 이제 공개적으로 0인 애들이니까 얘를 반환하면 됨
		}
	 }
	 
	 //printf("여기가 문제야?\n");
	 //if (victim == list_entry(list_end(&frame_list), struct frame, elem)) {
	 if (frame == list_end(&frame_list)) {
		// 만약에 이 for문이 끝까지 갔다면, 이 frame list 안에서 victim이 없다는 말임. 다 최근에 access되었다는 뜻이겠지..
		// 그러면 이제 여기서도 앞 for문에서 0으로 바꿔주었기 때문에, 무조건 첫 번쨰 애가 0이겠지
		// 그러면 그냥 첫 번째 애를 반환하면 될듯!!
		//victim = list_entry(list_begin(&frame_list), struct frame, elem);
		return vm_get_victim(); // 끝까지 갔으면 한 번 더 돌린다
	 }
	return victim;
	
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	// victim frame을 swap out하고, 이렇게 완전 비워진 frame을 return하는 함수임!
	// 즉, 이미 최근에 access되었으면 통과, 아니면 victim으로 select 되어야 한다.
	bool swap_out_well = swap_out(victim->page); // vm_get_victim을 통해 victim을 get해야 한다
	if (swap_out_well) {
		//victim->page = NULL; // frame도 reset해주는 과정
		return victim;
	} else {
		return NULL;
	}
	//return victim;
	//return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	//printf("마지막에 여기가 에러 0?\n");
	//struct frame *frame = NULL;
	//struct frame *frame;
	/* TODO: Fill this function. */
	// gitbook을 보면 이걸 제일 먼저 채우라고 적혀있음
	// 위 설명을 읽어보면, 
	/* 1. user pool로부터 새로운 physical page를 얻는다 (palloc_get_page 이용) */
	struct frame *frame = (struct frame*)malloc(sizeof(struct frame)); // 먼저 크기 배정
	//frame = (struct frame*)malloc(sizeof(struct frame)); // 먼저 크기 배정
	//printf("frame malloc이 안돼?\n");
	//printf("아니면 PAL_USER이 안돼?\n");
	frame->kva = palloc_get_page(PAL_USER); // user pool로부터 새로운 page 얻음
	//printf("아니면 PAL_USER이 안돼?\n");
	// 여기서, 만약 이미 빈 페이지였다면 그냥 그대로 return하면 되는데,
	// 이미 frame이 다 차 있어서 빈 페이지가 더이상 남아있지 않다면 victim을 쫓아내고 빈칸으로 만들어야 함
	// ASSERT (frame != NULL);
	// ASSERT (frame->page == NULL);
	//printf("마지막에 여기가 에러 1? frame->kva: 0x%x\n", frame->kva);

	if (frame->kva != NULL) {
		// 이미 빈 칸이 있어서 그게 배치된 경우
		// 새로운 frame을 가져왔으니 page를 초기화해주고, frame list에 넣기
		//printf("마지막에 여기가 에러 2? frame->kva: 0x%x\n", frame->kva);
		frame->page = NULL;
		list_push_back(&frame_list, &frame->elem);
	} else {
		// 빈칸이 없어서 배당이 안 된 경우
		// victim을 evict하고, 새로운 frame으로 비운다
		// 위에 보니 vm_evict_frame()이라는 함수를 사용해야 하는 것 같음
		// 근데 gitbook에는 일단 panic("todo")를 사용하라고 적혀있는 것 같은데...
		//printf("마지막에 여기가 에러 3? frame->kva: 0x%x\n", frame->kva);
		//PANIC("todo");
		frame = vm_evict_frame();
		//printf("마지막에 여기가 에러 3? frame->kva: 0x%x\n", frame->kva);
		frame->page = NULL; // null로 해주는건, 일단 page를 reset (init)해주는 과정임!
	}
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL); // 이렇게 위에서 page->NULL을 해줘야 이걸 통과하는 거였다. page를 init하는 과정 뒤에 놨어야 했는데 내가 븅신이지...

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr) {
	void *adjusted_addr = pg_round_down(addr);
	// struct page *found_page = spt_find_page(&thread_current()->spt, adjusted_addr);
	// // allocating one or more pages so that addr is no longer faulted이라고 하니까 
	// // 한번만 alloc하는게 아니라 가능할동안 계속 할당해주는거다
	// while (found_page == NULL) {
	// 	bool alloc_success = vm_alloc_page(VM_ANON | VM_MARKER_STACK, adjusted_addr, true);
	// 	bool claim_success = vm_claim_page(adjusted_addr);
	// 	//다음 페이지 주소로 내려간 후 다시 alloc 시도해서 가능하면 계속 페이지 단위로 할당해준다
	// 	if (alloc_success && claim_success) {
	// 		memset(adjusted_addr, 0, PGSIZE);
	// 		adjusted_addr += PGSIZE;
	// 	}
	// 	found_page = spt_find_page(&thread_current()->spt, adjusted_addr);
	// }
	bool alloc_success = vm_alloc_page(VM_ANON | VM_MARKER_STACK, adjusted_addr, true);
	while (alloc_success) {
		struct page *pg = spt_find_page(&thread_current()->spt, adjusted_addr);
        vm_claim_page(adjusted_addr);
        adjusted_addr += PGSIZE;
		alloc_success = vm_alloc_page(VM_ANON | VM_MARKER_STACK, adjusted_addr, true);
	}

	//vm_alloc_page(VM_ANON | VM_MARKER_STACK, adjusted_addr, true);
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr,
		bool user, bool write, bool not_present) {
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = NULL;
	//printf("handle fault addr: 0x%x\n", addr);
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	//search the disk, allocate the page frame and load the appropriate page from the disk to the allocated page
	//일단 주어진 address가 valid한지 체크를 먼저 하자
	//rsp가 유저스택을 가르키고 있으면 이 스택 포인터를 그대로 써도 되는데 커널 스택을 가르키고 있다면 유저->커널로 바뀔때의 thread내의 rsp를 사용해야된다
	bool success = true;
	//유저는 자신의 가상공간만 접근할 수 있기 때문에 커널 가상 메모리에 접근하려고 하면 바로 프로세스 종료 시켜야한다
	//if (is_kernel_vaddr(addr) && user || addr == NULL || not_present == false) {
	if (is_kernel_vaddr(addr) && user || not_present == false) {
		//printf("kernel에 가려고 했어?\n");
		//printf("not present: %d\n", not_present == false);
		//printf("is_kernel_vaddr(addr) && user: %d\n", is_kernel_vaddr(addr) && user);
		//printf("addr == NULL: %d\n", addr == NULL);
		return false;
	} else {	//not_present인 경우에는 물리 프레임이 할당되지 않아서 발생한 fault이기 때문에 이때 페이지랑 물리 프레임을 연결시켜준다
		//spt에서 페이지 fault가 일어난 페이지를 찾아야한다
		//printf("spt_find_page 전이야?\n");
		page = spt_find_page(spt, addr);
		if (page == NULL) {	//스택이 가득차서 할당이 더 이상 불가능한 경우
			//printf("아니면 page가 null인 상태야?\n");
			//NOT_REACHED();
			/*
			const int STACK_SIZE = 0x100 * PGSIZE;
			*/
			//printf("spt_find_page 후 page가 null일 떄야?\n"); // 0x0. 여기다.
			const uint64_t STACK_SIZE = 0x100000;
			const uint64_t STACK_LIMIT = USER_STACK - (1<<20);
			//interrupt frame의 rsp주소를 받아와서 이게 커널 영역인지 유저 스택 영역인지 체크하고 
			//유저프로그램에서 fault 발생한 경우에는 그냥 이 rsp를 가지고 쓰고 커널에서 발생한거면 쓰레드에 저장해놓은 유저 스택 rsp정보를 받아와야한다
			uintptr_t pointer = f->rsp;
			//NOT_REACHED();
			if (!user) {
				//printf("혹시 여기야?\n");
				pointer = &thread_current() -> user_stack_rsp;
				//printf("pointer: 0x%x\n", pointer);
				//pointer = &thread_current() -> if_for_fork.rsp;
			}
			//일단 stack의 1MB 범위내에 addr가 있는지 확인을 하자
			if (addr <= USER_STACK && addr >= USER_STACK - STACK_SIZE && addr >= pointer - 8) {
				//현재 addr가 현재 유저 스택의 가장 아래 위치보다 더 아래 위치를 접근하려고 하면 스택을 더 크게 늘려줘야한다
				vm_stack_growth(addr);
			} else if (addr <= USER_STACK && pointer >= USER_STACK - STACK_SIZE && addr >= pointer) {
				//NOT_REACHED();
				vm_stack_growth(addr);
			// if (addr > STACK_LIMIT && USER_STACK > (uint64_t) addr > (uint64_t)pointer - STACK_SIZE) {
			// 	NOT_REACHED();
			// 	vm_stack_growth(pg_round_down(addr));
			} else {
				//printf("혹시 여기 들어가?\n");
				success = false;
			}
		} else {
			if (write && page->write == false) {
				//읽기 전용 페이지에 쓰려고 한 경우는 불가능하다
				//printf("아니면 읽기 전용?\n");
				success = false;
			} else {
				//spt에 이미 들어가있는 페이지이기 때문에 이걸 물리 프레임이랑 매핑해준다
				//NOT_REACHED();
				ASSERT(!is_kernel_vaddr(addr));
				ASSERT(page != NULL);
				ASSERT(!(write && page->write == false));
				success = vm_do_claim_page(page);
				//printf("분명히 return true를 했을텐데,...\n");
			}
		}
	}
	
	return success;
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
	//printf("어디서 에러가 나는거야 1\n");
	//printf("(vm_do_claim_page) page addr: 0x%x\n", page);
	struct frame *frame = vm_get_frame ();
	//printf("어디에러4\n");

	if (frame == NULL) {
		//printf("어디서 에러가 나는거야 2\n");
		return false; // get frame 했는데 NULL이면 당연히 mapping 불가하므로 false 반환
	}

	/* Set links */
	frame->page = page;
	page->frame = frame;
	//printf("어디에러 6\n");

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// gitbook 설명대로라면, 위에서 vm_get_page로 frame을 갖고 온 다음에,
	// MMU를 setting해준다. 즉, page table 안에서 va와 pa를 mapping하는걸 추가한다.
	// 그리고, 잘 완료되었으면 true, 아니면 false를 반환한다
	bool mapping_va_pa = install_page_in_vm(page->va, frame->kva, page->write);
	//bool mapping_va_pa = pml4_set_page(&thread_current()->pml4, page->va, frame->kva, page->write);
	if (mapping_va_pa) {
		//printf("어디서 에러가 나는거야 3\n"); // 여기서 swap 하면서 에러가 일어난다.
		return swap_in (page, frame->kva);
	} else {
		//printf("어디서 에러가 나는거야 4/n");
		return false;
	}

	//return swap_in (page, frame->kva);
}

static bool
install_page_in_vm (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	//그냥 해쉬테이블을 init해주면된다
	/*
	hash_init(&(spt->page_table), spt_hash, spt_compare, NULL);
	*/
	hash_init(&spt->page_table, spt_hash, spt_compare, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	//when child needs to inherit execution code of its parent
	//src의 spt의 모든 페이지 관련 정보들을 dst의 spt안에 넣는다
	//src의 spt를 iterate해야하는데 이건 지금 hash table 구조체를 가지고 있으니 hash_iterator을 사용해야한다
	bool success = true;
	struct hash_iterator spt_iterator;
	//spt_iterator.hash = &(src->page_table);
	//각 iteration마다 각 hash_elem랑 연결되어 있는 페이지를 찾아서 이 페이지를 dst의 spt에 넣도록 한다
	hash_first(&spt_iterator, &(src->page_table));
	while(hash_next(&spt_iterator)) {
		struct page *src_page = hash_entry(hash_cur(&spt_iterator), struct page, hash_elem);
		//vm_alloc에서 보이다시피 vm_type, upage, writable, init, aux 정보들을 다 담아서 dst spt안에 넣어줘야한다
		enum vm_type src_page_type = src_page->operations->type;
		if (src_page_type == VM_ANON) {
			//일단 dst의 spt에 들어갈 pending 페이지를 하나 만들어주고 그 다음에 여기 안에 들어가야하는 정보들을 다 넣는다
			//이렇게 vm_alloc 사용하면 spt_insert_page를 통해서 이 페이지가 spt에 들어간다
			bool alloc_success = vm_alloc_page(src_page_type, src_page->va, src_page->write);
			if (!alloc_success) {
				return false;
			}
			//allocate한 후에 바로 claim을 해서 이 페이지랑 프레임으로 매핑시켜줘야한다
			bool claim_success = vm_claim_page(src_page->va);
			if (!claim_success) {
				return false;
			}
			//즉, 이 페이지가 잘 들어갔는지 확인하기 위해서 spt_find_page를 사용해서 NULL인지 아닌지를 확인할 수 있다
			struct page *dst_page = spt_find_page(dst, src_page->va);
			if (dst_page == NULL) {
				// success = false;
				// break;
				return false;
			} 
			//매핑된 프레임에다가 내용을 넣어야한다
			//src page의 프레임 주소에 맞는 커널 구역에 들어가서 여기에서의 하나의 프레임의 내용을 그래도 복사해서 dst의 해당 page에 맞는 프레임의 커널 구역에 내용 넣기
			memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
		} else if (src_page_type == VM_UNINIT) {
			if (src_page->uninit.type == VM_ANON) {
				vm_initializer *src_init = src_page->uninit.init;
				/*
				void *src_aux = src_page->uninit.aux;
				*/
				void *src_aux = (struct segment_info *)malloc(sizeof(struct segment_info));
				memcpy(src_aux, src_page->uninit.aux, sizeof(struct segment_info));

				bool alloc_success = vm_alloc_page_with_initializer(VM_ANON, src_page->va, src_page->write, src_init, src_aux);

				if (!alloc_success) {
					return false;
				}
			}
			//continue;
		}
	}
	return success;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	//iterate through the spt and destroy(page) for all pages
	
	// struct hash_iterator spt_iterator;
	// hash_first(&spt_iterator, &(spt->page_table));
	// while (hash_next(&spt_iterator)) {
	// 	struct page *spt_page = hash_entry(hash_cur(&spt_iterator), struct page, hash_elem);
	// 	if (spt_page->operations->type == VM_FILE) {
	// 		do_munmap(spt_page->va);
	// 	}
	// }
	
	hash_clear(&(spt->page_table), destroy_page_table);
}

void destroy_page_table (struct hash_elem *e, void *aux) {
	destroy(hash_entry(e, struct page, hash_elem));
}

//spt를 해쉬테이블 구조로 쓰기 때문에 페이지 입력할때 어떻게 넣을지에 대한 해쉬 함수를 적어줘야한다
//pintos가 쓰라는 방식으로 address를 key로 설정해서 해쉬한다
uint64_t
spt_hash (const struct hash_elem *elem, void *aux) {
	const struct page *p = hash_entry(elem, struct page, hash_elem);
	return hash_bytes(&p -> va, sizeof(p->va));
}

//해쉬 테이블에서 각 key들을 비교해서 같은 key로 어떤 element가 들어오지 않도록 해야한다
//즉, 해쉬 테이블의 각 element를 비교하는 함수를 만들어줘야한다
bool
spt_compare (const struct hash_elem *a, const struct hash_elem *b, void *aux) {
	const struct page *pa = hash_entry(a, struct page, hash_elem);
	const struct page *pb = hash_entry(b, struct page, hash_elem);

	return pa->va < pb->va;
}