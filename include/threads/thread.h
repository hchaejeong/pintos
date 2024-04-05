#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

#define TIMER_FREQ 100		//몇개의 tick이 1초에 일어나는지

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */
	int nice;							//nice value
	int recent_cpu;						//cpu value

	//각 thread의 local tick attribute을 저장해놔야함
	int64_t wakeup_tick; 

	// 자신의 원래 priority를 저장해두기
	int origin_priority;
	// 내가 어떤 lock을 대기타고 있는지 저장
	struct lock *what_lock;
	// lock 요청하는 thread들 저장하기
	struct list wanna_lock_threads;
	// lock 요청하는 thread를 list 형태로 저장하기 위한 새로운 elem
	// 원래 있었던, 아래의 list_elem을 썼더니 그냥 간섭이 일어나서 그런지
	// 계속 에러가 뜬다. 몇번을 시도해도 그런거면... 그냥 내가 코딩을 못하는건지
	// 진짜 간섭이 일어나서 어쩔수가 없는건지 어쨌든 새로운 형태의 elem을 만들어야 겠다.
	struct list_elem what_lock_elem;

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif	
	//user app을 사용할때 file descriptor table으로 파일에 접근할수있도록 한다 (현재 Unix은 모든것을 파일로 관리하고 있다 - 시스템 콜도)
	//file descriptor table의 인덱스로 어떤 시스템 콜 또는 파일을 access하도록 한다
	//각 쓰레드는 이 파일 테이블이 필요하고 프로세스에서 시스템 콜으로 파일을 열때마다 파일 인덱스가 하나씩 +1 된다

	//struct file을 가르키는 포인터를 여러개 가지고 있어 list로 만들어서 이 리스트의 index가 fd가 되도록 하고 content에는 파일을 가르키는 포인터를 넣어놓자	
	//결국 이 file_descriptor_table은 fd_structure (fd_index랑 파일 포인터를 가지고 있는 struct)의 테이블이다
	//따라서, 각 fd_structure를 tracking하고 쉽게 빼올수있기 위해선
	struct list file_descriptor_table; 	//각각 파일을 포인트하고 있는 원소들의 리스트 형태로 관리해둔다
	int curr_fd;	//지금 쓰레드가 실행하고 있는 파일이 들어가있는 fd 인덱스
	struct file *executing_file;

	int exit_num; // exit할 때 어떤 exit인지 적어줘야 함

#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_recalculations(void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

//compare_priority_thread 함수 추가
bool compare_priority_func(const struct list_elem *a,
                             const struct list_elem *b,
                             void *aux);

// what_lock_elem을 위한 priority compare 함수
//compare_priority_thread 함수 추가
bool lock_compare_priority_func(const struct list_elem *a,
                             const struct list_elem *b,
                             void *aux);

// running thread와 ready list의 thread의 priority 비교해주는 함수
bool check_ready_priority_is_high(void);

// 전체 함수 선언에도 우리가 만든 함수를 추가해줘야 함
void thread_sleep(int64_t);
void thread_wakeup(int64_t);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

int calculate_priority(struct thread *);
int calculate_recent_cpu(struct thread *);
int calculate_load_avg(void);
void recalculate_recent_cpu(void);
void recalculate_priority(void);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */