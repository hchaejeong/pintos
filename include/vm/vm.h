#ifndef VM_VM_H
#define VM_VM_H
#include <stdbool.h>
#include "threads/palloc.h"
#include <hash.h>

enum vm_type {
	/* page not initialized */
	VM_UNINIT = 0,
	/* page not related to the file, aka anonymous page */
	VM_ANON = 1,
	/* page that realated to the file */
	VM_FILE = 2,
	/* page that hold the page cache, for project 4 */
	VM_PAGE_CACHE = 3,

	/* Bit flags to store state */

	/* Auxillary bit flag marker for store information. You can add more
	 * markers, until the value is fit in the int. */
	VM_MARKER_0 = (1 << 3),
	VM_MARKER_1 = (1 << 4),
	VM_MARKER_STACK = (1 << 5),

	/* DO NOT EXCEED THIS VALUE. */
	VM_MARKER_END = (1 << 31),
};

#include "vm/uninit.h"
#include "vm/anon.h"
#include "vm/file.h"
#ifdef EFILESYS
#include "filesys/page_cache.h"
#endif

struct page_operations;
struct thread;

#define VM_TYPE(type) ((type) & 7)

/* The representation of "page".
 * This is kind of "parent class", which has four "child class"es, which are
 * uninit_page, file_page, anon_page, and page cache (project4).
 * DO NOT REMOVE/MODIFY PREDEFINED MEMBER OF THIS STRUCTURE. */
struct page {
	const struct page_operations *operations;
	void *va;              /* Address in terms of user space */
	struct frame *frame;   /* Back reference for frame */

	/* Your implementation */
	//결국 페이지의 정보를 spt가 가지고 있을테니 spt에 이 struct page구조체가 들어가게 되니까 해쉬 정보를 가지고 있어야한다
	struct hash_elem hash_elem;
	bool write;

	/* Per-type data are binded into the union.
	 * Each function automatically detects the current union */
	union {
		struct uninit_page uninit;
		struct anon_page anon;
		struct file_page file;
#ifdef EFILESYS
		struct page_cache page_cache;
#endif
	};
};

/* The representation of "frame" */
// frame은 pa와 매핑할 때 필요함.
// 결국에는 기존 pml4 table에 없는 정보를 추가적으로 저장하기 위해 사용됨.
struct frame {
	void *kva; // kernel va
	struct page *page;
	struct list_elem elem; // list로 사용하기 위함. page도 page table, pml4도 table이니까 frame도 table형태로!
};

/* The function table for page operations.
 * This is one way of implementing "interface" in C.
 * Put the table of "method" into the struct's member, and
 * call it whenever you needed. */
struct page_operations {
	bool (*swap_in) (struct page *, void *);
	bool (*swap_out) (struct page *);
	void (*destroy) (struct page *);
	enum vm_type type;
};

//파일 내용을 lazy_load_segment으로 넘겨줄때 필수적으로 넘겨줘야하는 정보들
struct segment_info {
	struct file *page_file;
	off_t offset;
	uint32_t read_bytes;
	uint32_t zero_bytes;
};

#define swap_in(page, v) (page)->operations->swap_in ((page), v)
#define swap_out(page) (page)->operations->swap_out (page)
#define destroy(page) \
	if ((page)->operations->destroy) (page)->operations->destroy (page)

/* Representation of current process's memory space.
 * We don't want to force you to obey any specific design for this struct.
 * All designs up to you for this. */
struct supplemental_page_table {
	//pml4는 단순히 virtual address를 physical address로 변환해주는 역할만 하지 실제 사용될 페이지에 대한 어떠한 정보도 가지고 있지 않다
	//SPT가 각각 페이지에 대한 정보를 추가적으로 보충해줘야한다 
	//page fault가 일어날때 이 페이지에 담겨있는 정보들을 접근해야하기 때문에 필요하고
	//thread를 종료시킬때 어떤 데이터가 할당 해제가 되어야하는지 알기 위함
	//pintos가 제공해주는 해쉬 테이블을 사용해보자
	//해쉬 테이블의 각 entry는 linked list으로 해당 key에 맞는 주소를 가진 원소들의 집합으로 이루어져있다
	struct hash page_table;
};

#include "threads/thread.h"
void supplemental_page_table_init (struct supplemental_page_table *spt);
bool supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src);
void supplemental_page_table_kill (struct supplemental_page_table *spt);
struct page *spt_find_page (struct supplemental_page_table *spt,
		void *va);
bool spt_insert_page (struct supplemental_page_table *spt, struct page *page);
void spt_remove_page (struct supplemental_page_table *spt, struct page *page);

void vm_init (void);
bool vm_try_handle_fault (struct intr_frame *f, void *addr, bool user,
		bool write, bool not_present);

#define vm_alloc_page(type, upage, writable) \
	vm_alloc_page_with_initializer ((type), (upage), (writable), NULL, NULL)
bool vm_alloc_page_with_initializer (enum vm_type type, void *upage,
		bool writable, vm_initializer *init, void *aux);
void vm_dealloc_page (struct page *page);
bool vm_claim_page (void *va);
enum vm_type page_get_type (struct page *page);

uint64_t spt_hash (const struct hash_elem *e, void *aux);
bool spt_compare (const struct hash_elem *a, const struct hash_elem *b, void *aux);

#endif  /* VM_VM_H */
