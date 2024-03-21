#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/fixed_point.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

// sleep_list 추가
// THREAD_BLOCK state에 있는 process list
static struct list sleep_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;
//mlfq scheduling에 필요한 변수
int load_avg = 0;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&sleep_list); // sleep_list initalization
	// init을 하면 empty list가 만들어지는 거니까,
	// 우리가 sleep_list에 add해주는 상황에서만 wake_up_time에 맞춰서 추가해주면 됨
	list_init (&destruction_req);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);

	//nice랑 recent_cpu를 일단 0으로 초기화하기
	initial_thread->nice = 0;
	initial_thread->recent_cpu = 0;
	load_avg = 0;

	//wakeup_tick attribute를 초기화 해줘야하나...?
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	//booting하면 load_avg = 0
	load_avg = 0;
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

//advanced scheduler인 경우 각 thread의 load_avg, recent_cpu, priority를 1초마다 계산하고
//모든 thread의 priority는 매 4번의 틱마다 다다시 계산해줘야함.
void
thread_recalculations(void) {
	if (!thread_mlfqs)
		return;
	
	int64_t tick_num = timer_ticks();
	struct thread *current = thread_current();
	int thread_recent_cpu = current->recent_cpu;

	//recent_cpu는 매 tick마다 1씩 올라감
	current->recent_cpu = add_fp_int(thread_recent_cpu, 1);
	
	if (tick_num % TIMER_FREQ == 0) {
		load_avg = calculate_load_avg();
		recalculate_recent_cpu();
	}
	if (tick_num % TIME_SLICE == 0) {
		recalculate_priority();
	}
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	thread_unblock (t);

	// 위에 영어로 된 설명 읽어보니까, ordering은 너가 알아서 해라
	// & priority scheduling도 너가 알아서 해라 라는 의미 같음.
	// 그런데 여기서 더 ordering이 필요할까 싶기는 한데
	// (thread_unblock())에서 list_insert_ordered를 썼으니까 순서대로 갔으리라 믿으나)
	// 그래도 혹시 모르니까 또 sort 해주는게 좋지않을깡?
	list_sort(&ready_list, compare_priority_func, NULL);
	// 그다음에 이제 priority가 높은 애들은 바로 running 되어야 하니까,
	// 만들어놨던 check ready pri..이 함수 쓰면 되지!
	if (check_ready_priority_is_high()) {
		thread_yield();
	}

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	// list_push_back (&ready_list, &t->elem);
	// push_back 말고 list_inserted_ordered로 ready_list에다가 넣어야 됨
	list_insert_ordered(&ready_list, &t->elem, compare_priority_func, NULL);
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

//list_sort랑 list_insert_ordered와 같은 함수들을 사용하기 위해서는 list_less_func이라는 
//typedef 형식을 따라서 a > b일때 true로 나오도록 해서 descending order으로 sorting 가능하게함.
   //typedef는 return type을 bool로 하고 parameter를 const struct list_elem * 두개를 받는 형태의 function이면됨.
bool
compare_priority_func (const struct list_elem *a,
                  const struct list_elem *b,
                  void *aux) {
   struct thread *a_thread = list_entry(a, struct thread, elem);
   struct thread *b_thread = list_entry(b, struct thread, elem);

   return a_thread->priority > b_thread->priority;
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

// synch.c에서 semaphore을 하다보니 현재 running되고있는 thread랑
// ready list의 가장 앞에있는 thread랑 priority 비교해서 높은 애로 대체되는
// 함수가 없길래 만들러 왔다
// 일단 priority가 더 높은지 check하는 함수를 만들면 되지 않을까
bool
check_ready_priority_is_high(void) {
	// ready_list의 thread와 running thread의 priority 비교
	// true면 ready list의 prioirty가 더 높음
	// false면 running priority가 더 높음
	if (!list_empty(&ready_list)) {
		// 일단 혹시모르니 한번더 sort하는게 좋지않을까 (보험)
		list_sort(&ready_list, compare_priority_func, NULL);
		// if (thread_current()->priority < next_thread_to_run()->priority) {
			// -> 얘의 문제를 알아냈다. next_thread_to_run()을 쓰면 front에 있는 애가
			// POP!!!이 된다. 즉 없어진다... 확인만 하는 용도인데 POP 되어버리면
			// 엉뚱한 애가 되겠지 ㅜㅜㅜㅜㅜㅜ
			// 아싸!!! 이거 해결하니까 priority-sema 풀렸다!!!
		if (thread_current()->priority < list_entry(list_begin(&ready_list), struct thread, elem) -> priority) {
			// 다음에 실행시킬애가 priority가 더 높으면 return true
			return true;
		} else {
			return false;
		}
	} else {
		return false; // ready list에 있는 애가 아무도 없으면 걍 돌아야지 뭐 어쩌겐
	}
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
	//list_push_back (&ready_list, &curr->elem);
	list_insert_ordered(&ready_list, &curr->elem, compare_priority_func, NULL);
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

// thread_yield와 비슷하게 thread_sleep을 만들자.
// ready_list 대신에 sleep_list에 넣고, BLOCK 상태로만 만들면 된다.
void
thread_sleep(int64_t wake_up_time) {
	// 여기서, idle thread는 BLOCK을 하면 안됨
	// disable interrupt를 한 상황에서
	// sleep_list에 이 thread를 포함시키고,
	// thread의 state를 BLOCKED로 만들고
	// wake_up_time을 이 thread에 store하고,
	// 필요시 global_tick을 update하고,
	// schedule()을 부른다
	// enable interrupt를 한다
	
	//bool go = true;
	struct thread *current;
	enum intr_level old_level;
	//struct list_elem *tmp;
	//disable interrupt하고 그 전 interrupt state를 반환

	ASSERT (!intr_context ());
	
	old_level = intr_disable();
	current = thread_current();

	if (current != idle_thread) {
		// &sleep_list를 next로 쭉쭉 돌리면서
		// (list_begin() 함수로 sleep_list의 begin을 갖고오고)
		// (list_next() 함수로 sleep_list를 하나하나 순서대로 둘러봄)
		// wake_up_time이랑 sleep_list안의 thread의 wake_up_time을 비교해서
		// wake_up_time보다 크거나 같으면 그 thread를 반환
		// 그러면, list_insert() 함수를 이용해서 삽입.
		current->wakeup_tick = wake_up_time; // wakeup_tick 설정
		// if (wake_up_time > timer_ticks()) {
		// 	wake_up_time = timer_ticks();
		// }
		// 먼저, sleep_list가 비었는지 확인
		if (list_empty(&sleep_list)) {
			list_push_back(&sleep_list, &current->elem);
		} else {
			struct list_elem *tmp;
			// Hㅏ... list.h에 친절하게 for문 쓰는 방법 설명해둔걸
			// 왜 이제서야 읽었을까!!!!! 으악이렇게 간단해질수가!!!!!
			// 계속 list_next()의 ASSERT에서 걸렸었는데!!!!
			for (tmp = list_begin (&sleep_list); tmp != list_end (&sleep_list); tmp = list_next (tmp)) {
				struct thread *t = list_entry (tmp, struct thread, elem);
				if (t->wakeup_tick >= wake_up_time) {
					// wake_up_time보다 크거나 같은 wakeup_tick을 가진 elem을 반환
					break;
				}
			}

			// ASSERT (list_begin(&sleep_list) != NULL);
			// ASSERT (list_begin(&sleep_list) != list_tail(&sleep_list));
			// tmp = list_begin(&sleep_list);
			// while (go) {
			// 	if (tmp == list_tail(&sleep_list)) {
			// 		go = false;
			// 		break;
			// 	} else if (list_entry(tmp, struct thread, elem)->wakeup_tick >= wake_up_time) {
			// 		// wake_up_time보다 크거나 같은 wakeup_tick을 가진 elem을 반환
			// 		go = false;
			// 		break;
			// 	} else {
			// 		ASSERT (tmp != NULL);
			// 		ASSERT (tmp != list_tail(&sleep_list));
			// 		tmp = list_next(tmp);
			// 	}
			// }

			if (tmp == list_begin(&sleep_list)) {
				list_push_front(&sleep_list, &current->elem);
			} else if (tmp == list_tail(&sleep_list)) {
				list_push_back(&sleep_list, &current->elem);
			} else {
				list_insert(tmp, &current->elem);
			}
		}
		//list_push_back (&sleep_list, &current->elem);	//이 부분을 wakeup_tick이 작은순으로 넣기
		thread_block();
	}
	//do_schedule (THREAD_READY);
	//current->wakeup_tick = wake_up_time; // wakeup_tick 설정

	//do_schedule (THREAD_BLOCKED);
	intr_set_level (old_level);
}

//timer interrupt에서 thread를 wake up 시키는 함수
void
thread_wakeup(int64_t wake_up_tick) {
	/*
	sleep list의 element들을 하나씩 체크해서 wake_up_tick보다 thread의 wakeup_tick이 작으면 ready list로 옮겨줘야함
	sleep list를 wakeup_time이 작은 순서대로 정렬을 했기 때문에 wake_up_tick보다 큰 element을 도달하면 나머지 element들을 확인 안해도됨.
	
	struct list_elem *sleep_elem = list_begin(&sleep_list);
	struct thread *current = thread_current();

	ASSERT(!list_empty(&sleep_list));
	
	struct list_elem *sleep_elem;
	for (sleep_elem = list_begin (&sleep_list); sleep_elem != list_end (&sleep_list); sleep_elem = list_next(sleep_elem)) {
		struct thread *t = list_entry (sleep_elem, struct thread, elem);
		//if (t->wakeup_tick <= timer_ticks()) {
		if (t->wakeup_tick <= wake_up_tick) {
			sleep_elem = list_remove(sleep_elem);
			thread_unblock(t);
			if (sleep_elem == list_end(&sleep_list)) {
				break;
			}
		} else {
			break; // 시간이 더 커지면 더이상 볼 필요가 엇음
		}
	}
	지금 위에 적어둔 것들이 계속 woke up * ticks later의 *에 들어가는 숫자들이 잘못되어져서 나온다.
	0과 10만 나와야 하는데 계속 1과 9만 나온다고 해야할까... 이게 global tick의 문제인지
	아니면 일어나는 타이밍이 이상한건지 그걸 진짜 모르겠다.
	그래서 그냥 반복문 형태를 바꿔보기로 했다. 어차피 끝까지 돌아야 할 필요가 없으니까
	for loop 대신에 걍 while문 쓰고 계속 조건문을 통해서 break 하는 걸로 써보았다
	*/

	// 아니 그랬더니 성공했다 대체 뭐가 문제였던거지???? 아니 내가 위에서 뭘잘못했는뎁쇼
	struct list_elem *sleep_elem = list_begin(&sleep_list);
	while (sleep_elem != list_end(&sleep_list)) {
		struct thread *t = list_entry (sleep_elem, struct thread, elem);
		if (t->wakeup_tick > wake_up_tick) {
			// thread에 저장된 wake up tick보다 global tick이 더 작으면
			// 깨울 필요가 없고, 우리는 wake up tick 순서대로 sleep list에 넣어뒀으니
			// 어차피 커지는 순간 뒤를 볼 필요가 없다. 따라서 break
			break;
		} else {
			// 크지 않으면 그냥 다 빼내야 한다.
			sleep_elem = list_remove (sleep_elem);
			thread_unblock(t);
			if (sleep_elem != list_end(&sleep_list)) {
				// 만약 sleep_list에서 빼낸 에가 list의 마지막 애였다면?
				// 그러면 sleep_list를 다 본 거니까 break해야겠지!
				// 그게 아니라면 next로 넘기고.
				list_next(sleep_elem);
			} else {
				break;
			}
		}
		
	}

}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	//advanced scheduler를 사용할때는 disable
	if (!thread_mlfqs) {
		struct thread *current = thread_current();
		current->priority = new_priority;
		//현재 thread의 priority이 더 이상 가장 큰 priority를 가지고 있지 않을 경우 yield cpu
		//ready list의 priority를 다 살펴봐야함
		struct list_elem *ready_elem;

   enum intr_level old_level;
   old_level = intr_disable ();
   if (check_ready_priority_is_high()) {
		thread_yield(); // ready list에 있는 애가 더 크면 thread_yield()하면 됨!
   }
   intr_set_level (old_level);

//    //sort ready_list in decreasing order of priority   //then take the first element of the ready_list 
//    list_sort(&ready_list, compare_priority_func, NULL);
   
//    if (!list_empty(&ready_list)) {
//       struct list_elem *highest_priority = list_begin(&ready_list);
//       struct thread *ready_thread = list_entry(highest_priority, struct thread, elem);
//       if (ready_thread->priority > current->priority) {
//          thread_yield();
//       }
//    }

   // for (ready_elem = list_begin(&ready_list); ready_elem != list_end(&ready_list); ready_elem = list_next(&ready_list)) {
   //    struct thread *ready_thread = list_entry(ready_elem, struct thread, elem);
   //    if (ready_thread->priority > current->priority) {
   //       thread_yield();
   //    }
   // }
}
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	struct thread *current = thread_current();
	int priority_value = current->priority;

	return priority_value;
}

/* Sets the current thread's nice value to NICE. */
//recalculates the thread's priority based on the new value
//if the running thread no longer has the highest priority, yields
void
thread_set_nice (int new_nice UNUSED) {
	/* TODO: Your implementation goes here */
	struct thread *current = thread_current();
	enum intr_level old_level;
	old_level = intr_disable();

	current->nice = new_nice;
	//printf("new nice: ", new_nice);
	int recalculated_priority = calculate_priority(current);
	current->priority = recalculated_priority;
	
	if (check_ready_priority_is_high()) {
		thread_yield(); // ready list에 있는 애가 더 크면 thread_yield()하면 됨!
	}

	intr_set_level(old_level);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	struct thread *current = thread_current();
	int nice_value = current->nice;

	return nice_value;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	//updated exactly when the system tick counter reaches a multiple of a second
	//timer_ticks () % TIMER_FREQ == 0 일때만 업데이트시킨다
	//현재 시스템의 load_avg의 100배인 결과를 도출해야함 (rounded to nearest int)
	//load_avg는 시스템 내에서 동일한 값을 가진다 (따라서, 각 thread에 특정된 값이 아니라 global variable)
	struct thread *current = thread_current();

	int load_avg_100 = multiply_int_fp(load_avg, 100);
	int rounded_load_avg_100 = convert_to_int_nearest(load_avg_100);

	return rounded_load_avg_100;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	struct thread *current = thread_current();

	int current_recent_cpu = current->recent_cpu;
	int recent_cpu_100 = multiply_int_fp(current_recent_cpu, 100); 
	int rounded_recent_cpu_100 = convert_to_int_nearest(recent_cpu_100);

	return rounded_recent_cpu_100;
}

//priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)
int
calculate_priority(struct thread *thread) {
	//nice, priority는 integer이고 recent_cpu는 real number
	int thread_nice = thread->nice;
	int thread_recent_cpu = thread->recent_cpu;

	int nice_mul = 2 * thread_nice;
	int recent_cpu_div = divide_int_fp(thread_recent_cpu, 4);
	int combine_terms = add_fp_int(recent_cpu_div, nice_mul);
	int subtract_priority = subtract_int_fp((int)PRI_MAX, combine_terms);
	int priority_recovered = subtract_two_fp(0, subtract_priority);
	int priority_calc = convert_to_int_zero(priority_recovered);
	if (priority_calc >= PRI_MAX) {
		priority_calc = PRI_MAX;
	} else if (priority_calc <= PRI_MIN) {
		priority_calc = PRI_MIN;
	}

	return priority_calc;
}

//recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + nice
int
calculate_recent_cpu(struct thread *thread) {
	int recent_cpu = thread->recent_cpu;
	int nice = thread->nice;

	int first_term = multiply_int_fp(load_avg, 2);
	int second_term = add_fp_int(first_term, 1);
	int decay = divide_two_fp(first_term, second_term);
	int combine_terms = multiply_two_fp(decay, recent_cpu);
	int recent_cpu_calc = add_fp_int(combine_terms, nice);

	return recent_cpu_calc;
}

//load_avg = (59/60) * load_avg + (1/60) * ready_threads
int
calculate_load_avg() {
	struct thread *current = thread_current();
	int fraction1 = divide_two_fp(convert_to_fp(59), convert_to_fp(60));
	int fraction2 = divide_two_fp(convert_to_fp(1), convert_to_fp(60));
	int first_term = multiply_two_fp(fraction1, load_avg);
	//ready_threads is the number of threads that are running or ready to run at time of update 
	int ready_threads = 0;
	//executing하고 있는 현재 thread도 포함시켜야함 (idle thread이면 포함하지 않는다)
	if (current != idle_thread)
		ready_threads += 1;
	
	size_t ready = list_size(&ready_list);
	ready_threads += (int)ready;

	int second_term = multiply_int_fp(fraction2, ready_threads);
	int load_avg_calc = add_two_fp(first_term, second_term);

	return load_avg_calc;
}

void
recalculate_recent_cpu() {
	//모든 thread에 대해 recent_cpu를 recalculate해줘야함
	struct list_elem *e;
	struct thread *current = thread_current();
	current->recent_cpu = calculate_recent_cpu(current);
	//ready_list랑 sleep_list 둘 다 recalculate 해줘야함.
	for (e = list_begin (&ready_list); e != list_end (&ready_list); e = list_next (e)) {
    	struct thread *t = list_entry (e, struct thread, elem);
    	t->recent_cpu = calculate_recent_cpu(t);
	}
	for (e = list_begin (&sleep_list); e != list_end (&sleep_list); e = list_next (e)) {
    	struct thread *t = list_entry (e, struct thread, elem);
    	t->recent_cpu = calculate_recent_cpu(t);
	}
	//현재 돌아가고있는 thread랑 wait_list에 있는애들도 계산해줘야함??
}

void 
recalculate_priority() {
	//모든 thread에 대해 priority를 recalculate해줘야함
	struct list_elem *e;
	struct thread *current = thread_current();
	current->priority = calculate_priority(current);
	for (e = list_begin (&ready_list); e != list_end (&ready_list); e = list_next (e)) {
    	struct thread *t = list_entry (e, struct thread, elem);
    	t->priority = calculate_priority(t);
	}
	for (e = list_begin (&sleep_list); e != list_end (&sleep_list); e = list_next (e)) {
    	struct thread *t = list_entry (e, struct thread, elem);
    	t->priority = calculate_priority(t);
	}
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
	//initialize nice and recent_cpu
	t->nice = running_thread()->nice;
	t->recent_cpu = running_thread()->recent_cpu;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}
