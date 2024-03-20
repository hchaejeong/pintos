/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {

	// 내가 공유자원 사용하겠다고 요청하고, 만약 쓸 수 있으면 쓰는 상태로 바꿔야 하는 것
	// 근데 내가 공유자원 쓸 수가 없으면, 당연히 waiter 명단에 들어가야함
	// 지금 원래 상태는 list_push_back을 쓴 걸로 봐서 그냥 뒤에다가 넣는듯
	// 근데 우리는 priority로 순서 정해야하니까 그냥 그거만 바꾸면 될듯

	// 나 진짜 이거 checkout 돌려버려서 다 날아간게 진짜 너무 빡쳐 화나
	// 진짜 미치겠어 그저께 해서 하....복기를 할수있으려나 진짜 미치겠네

	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		// list_push_back (&sema->waiters, &thread_current ()->elem);
		// 채정이가 만든 priority 비교 함수 쓰면 됨. 기작은 같으니까
		list_insert_ordered(&sema->waiters,  &thread_current ()->elem, compare_priority_func, 0);
		thread_block (); // 안되는 애를 block한 뒤, 현재 ready list에 있는 애를 실행시킴 (schedule 함수 실행)
	}
	sema->value--;
	intr_set_level (old_level);
}

// github 잘 올라가는지 확인

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	// 여기서는 waiters list를 sort해야한다.
	// 그리고, up 했으니까 lock이 풀린거잖아? 이 순간에도 CPU에 누가 들어갈지 잘 봐야하므로
	// 일단 먼저 ready list 안에 있는 애들 중에 priority가 더 높으면 걔가 running 되어야한다는 걸 잊지말자

	// 이게 문제야 이걸 많이 바꿨는데 이게 다날아갔으면 진짜 미치겠네 하


	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)) {
		// thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
		// 하.. 그때 어떻게 했더라 일단은 혹시 모르니까 다시 sort 한 다음에...
		list_sort(&sema->waiters, compare_priority_func, NULL);
		// 그다음에 어차피 unblock 하는건 똑같음. 그래야 waiter 명단에서 나오는 거니까.
		// 그런데 이 상황에서, ready_list 중에 priority가 더 높으면 걔가 running 되어야함.
		// 내가 thread.c에 함수 만들어놨으니까 ㅇㅇ 그거 쓰면 될듯
		thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
	}
	sema->value++;
	if (check_ready_priority_is_high()) {
		// 이건 ready list 애가 priority가 더 높은거니까 yield() 해야함
		thread_yield();
	}
	// 하... 난 진짜 미친X야 대체 뭐라고 아무생각없이 checkout을 한거니진짜로
	// 생각보다 코드가 간단했었기에 망정이지 이걸 엄청 짜고 난다음에 하면은 진짜 미친거아님?
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL); // lock이 null이 아니어야함. (당연)
	ASSERT (!intr_context ()); // 외부 interrupt가 없어야함?
	ASSERT (!lock_held_by_current_thread (lock)); // lock이 현재 실행되는 current thread에 걸려있지 않아야.

	sema_down (&lock->semaphore); // 즉, 지금 lock이 풀려있는 상황이면 sema 1->0 해주고 block.
	lock->holder = thread_current ();
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock)); // 얘는 acquire과 반대!
	// current thread가 lock되어있으면 그걸 풀어주는거지~

	lock->holder = NULL; // lock holder을 null로 만들어준 뒤
	sema_up (&lock->semaphore); // 가장 앞에 있는 애가 lock을 선점하게 해준다
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ()); // 외부 interrupt 없어야하고
	ASSERT (lock_held_by_current_thread (lock)); // current thread가 lock 잡고있을 때

	// 그니까 얘는 thread가 아니라 세마포를 갖고오는 애인건가
	// 그렇지. thread에는 priority 변수밖에 없고 sema는 아예 다른거니까
	// 원래 위에 sema 함수들에서 썼던 waiter list는 thread를 위한 리스트였고
	// 지금 waiter list는 semaphore만을 위한 list인 거겠지 싶음
	// 아니 근데 semaphore... list가 필요가있..나..?
	// 현재 running 되고있는 애랑 이제 sema값을 서로 바꿔야 하니까 그런건가...
	// 어쨌든 이걸 수정해야 하는 이유는 priority-condvar test 때문인 듯.
	// Signaling...이 여기로 signal 보내는 거일 것 같다
	// 근데 왜 이건 하지 않으면 30부터가 아니라 23부터 woke up 하는걸까?

	sema_init (&waiter.semaphore, 0);
	//list_push_back (&cond->waiters, &waiter.elem);
	
	// 이제 또 이 waiters list에서 priority 순서대로 해야겠지
	// 헿 모 다 비슷비슷하넹 ㅎㅎ
	//list_insert_ordered (&cond->waiters, &waiter.elem, compare_priority_func, NULL);
	// 하... ㅜ 왜 아무것도 안바뀌나 했더니 compare_priority_func는 thread일때만 사용할 수 있네
	// 이게뭐야... 또 만들어야하자나ㅜㅜㅜ
	// struct list *semaphore_waiters = &cond->waiters;
	// struct semaphore_elem *semaphore_waiter = list_entry(list_rbegin(semaphore_waiters), struct semaphore_elem, elem);
	// struct list *thread_waiters = &semaphore_waiter->semaphore.waiters;
	// // 하이씨 그러면 thread_waiters에 있는 thread 들이랑
	// // waiter.elem에 있는 semaphore_elem이랑 지금 형태가 다를거잖아 미치겟네
	// // 그래서 이게 안되는 거였구나.... 뭐 계속 unexpected interrupt가 뜨는데 ㅜ
	// list_insert_ordered (thread_waiters, &waiter.elem, compare_priority_func, NULL);

	// 함수 굳이 만들어야 하나 싶었는데 만들어야 할 듯... 하
	// 만들었다!
	list_insert_ordered(&cond->waiters, &waiter.elem, sema_compare_priority_func, NULL);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)) {
		
		/*
		여기서도 signal을 보내서, wake up 하게 해야한다.
		그게 바로 sema_up을 해주는거고,
		그런데 여기서도 혹시 모르니 sort를 해줘야 한다
		list_sort(&cond->waiters, compare_priority_func, NULL);
		printf("Ddddddddd");
		printf("%s", &list_entry(list_begin(&cond->waiters), struct thread, elem)->name);
		어.. 근데 왜 TIMEOUT이 뜰까
		음... condition의 waiters도 thread list인데 왜 안되지
		printf("%d", &list_entry(list_begin(&cond->waiters), struct thread, elem)->priority);
		이상하다. 어떻게 priority가 69459808이라는 값?처럼 클수가 있지?
		원래라면 이 값은 24? 여야 하는데, 왜 이따구로 큰 것일까.
		하 뭐가 문제지...
		&cond->waiters : 이건 list의 형태를 띔. 그 list 안에 뭐가 있는지는 모르는데
		일단 설명에는 List of waiting threads 라고 적혀있다고ㅜㅜ 그러면 thread list 아니냐고
		하.. 근데 cond_wait 함수에는 waiter을 semaphore_elem이라고 적어놨다.
		&cond->waiters의 waiter이겠지? 이건 확실할 듯
		그러면 &cond->waiters의 elem들은 모두 struct semaphore_elem으로 이루어진 애들이다
		즉, &cond->waiters는 semaphore_elem으로 이루어져 있다
		그럼 다시 프린트를 해보자
		printf("%s", list_entry(list_begin(&list_entry(list_begin(&cond->waiters), struct semaphore_elem, elem)->semaphore.waiters), struct thread, elem)->name);
		하.. 뭔가 알 것 같음
		&cond->waiters : struct semaphore_elem 들로 이루어진 list
		그러면 이걸 원래 하던 list_entry 함수를 써서 list의 특정 원소를, semaphore_list 형태로 꺼내면,
		&list_entry(&cond->waiters의 한 elem, struct semaphore_elem, elem)을 하면 된다.
		그러면 얘는 semaphore_elem의 형태일 것이다.
		
		struct semaphore_elem {
			struct list_elem elem; // list의 elem
			struct semaphore semaphore; // semaphore 그 자체
		};
		
		근데 여기서, semaphore이라는 struct는
		
		struct semaphore {
			unsigned value; //Current value.
			struct list waiters; //List of waiting threads.
		};
		
		이렇게 생겼다. 그러면 semaphore에는 waiters라는 list가 또 따로 있고,
		아니 근데 진짜 화나네 &cond->waiters도 list of waiting threads라매 진짜 어이X
		그러면, &list_entry.. 까지 부분을 tmp라 하면
		tmp->semaphore이 있고, 그거의 waiters를 불러온 다음에
		이 waiters는 thread의 list가 확실하니까 (위에 sema_down이랑 sema_up 함수 때 썼으니
		이제 여기서 다시 list_entry 이용해서 저 list 중 하나의 thread를 골라내면
		후... 이제 진짜 이 thread의 priority를 비교할 수 있게 된답니다 우하하
		이제 대충 깨달았네 으하하하하하 진짜 갈 길 멀다야
		*/

		/*
		// 그러면, 우리가 원래 알던 compare_priority_func 함수를 사용하려면,
		// thread의 list를 나타내는 tmp->semaphore.waiters 까지는 와야 하는 거잖슴
		// 그러면 하나하나 차근차근 해봅세

		struct list *semaphore_waiters = &cond->waiters;
		struct semaphore_elem *semaphore_waiter = list_entry(list_begin(semaphore_waiters), struct semaphore_elem, elem);
		struct list *thread_waiters = &semaphore_waiter->semaphore.waiters;
		// 그러면 이제 thread_waiter는 thread로 이루어진 list이다!
		// 이걸로 이제 sort 하면 될듯!!
		list_sort(thread_waiters, compare_priority_func, NULL);

		// 자, 차이가 없다고 생각했는데 이게 왜 안되는가 하면은,
		// 결국에 여기서는 cond 안에 있는 waiters를 sort 해야하는데
		// 나는 그냥 쓸데없이 thread_waiter에 있는 thread들을 sort하고 있었음
		// 그러니까 제대로 cond의 waiters가 sort 안된거지 ㅜ
		// 당연히 함수는 따로 만들어야 했던 거였음 뭐 어쩔수가 없었던 거였음
		*/

		// 결국 함수는 따로 만드는게 맞았던 것임.......
		list_sort(&cond->waiters, sema_compare_priority_func, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
}

// 이걸로 semaphore list의 priority를 비교하는 것.
// 일단 전체적인 맥락은 compare_priority_func와 비슷하겠지 싶음.
bool
sema_compare_priority_func(const struct list_elem *a, const struct list_elem *b, void *aux) {
	// 지금 위에 (cond_signal())에 무지막지하게 적어놓은 것을 참고하면 된다.
	// 결국에 여기서 input 되는 건 list_elem이고, 이건 semaphore_elem의 형태일 것이다.
	// 왜냐? &cond->waiters는 semaphore_elem이라는 elem으로 이루어져 있으니까.

	struct semaphore_elem *a_semaphore_waiter = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *b_semaphore_waiter = list_entry(b, struct semaphore_elem, elem);
	
	// 그럼 이제 각각 list를 빼내올 수 있게 되고,
	struct list a_thread_waiters = a_semaphore_waiter->semaphore.waiters;
	struct list b_thread_waiters = b_semaphore_waiter->semaphore.waiters;

	// 각 list의 thread의 priority를 빼내오면 된다! 이 부분은 compare_priority_func와 같은 형태
	struct thread *a_thread_waiter = list_entry(list_begin(&a_thread_waiters), struct thread, elem);
	struct thread *b_thread_waiter = list_entry(list_begin(&b_thread_waiters), struct thread, elem);

	return a_thread_waiter->priority > b_thread_waiter->priority;

}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
