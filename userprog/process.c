#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "userprog/syscall.h"
#ifdef VM
#include "vm/vm.h"
#endif

//struct lock *file_lock;

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	char * parsed;

	/* Create a new thread to execute FILE_NAME. */
	//command line을 파싱해서 얻은 파일 이름을 넘겨주고 thread을 새로 만든다 
	tid = thread_create (strtok_r(file_name, " ", &parsed), PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/
	//return thread_create(name, PRI_DEFAULT, __do_fork, thread_current());
	
	//printf("여기도 안 들어가?\n"); // 안 들어가네...
	struct thread *parent = thread_current();
	// do fork에 적어놨듯이, 현재 자신의 if를 미리 저장해놔야 나중에 fork하면서 자식에게 if가 전달됨
	memcpy(&parent->if_for_fork, if_, sizeof(struct intr_frame));
	// 자식(+본인) thread의 tid 생성. thread_create를 하자마자 child list에 넣어져있음!
	tid_t new_thread = thread_create (name, PRI_DEFAULT, __do_fork, thread_current());
	if (new_thread == TID_ERROR) {
		return TID_ERROR;
	}

	struct list *children = &parent->my_child;
	struct list_elem *child;
	if (list_empty(children)) {
		return TID_ERROR;
	}
	// children list를 돌면서 tid가 일치하는 thread를 찾음
	for (child = list_begin(children); child != list_end(children); child = list_next(child)) {
		if (new_thread == list_entry(child, struct thread, my_child_elem)->tid) {
			break;
		}
	}
	// 끝까지 돌았다는 건 child list에 없다는거니까 에러.
	if (child == list_end(children)) {
		return TID_ERROR;
	}
	// 진짜 child를 적어주고, fork sema를 down함으로써 child의 process가 끝날 때까지 parent는 대기
	struct thread *real_child = list_entry(child, struct thread, my_child_elem);
	sema_down(&real_child->sema_for_fork);
	// 만약 child가 error로 돌아왔으면 당연히 에러
	if (real_child->exit_num == -1) {
		return TID_ERROR;
	}

	return new_thread;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	// 하라는 대로 하자
	
	if (is_kernel_vaddr(va)) {
		//return false; // 난 커널에 있는게 에러라고 생각하고 false를 반환했는데, 아니다.
		// false를 반환해버리면, 그냥 duplicate가 중단되는 것이다.
		// 하지만 true를 반환하면 그냥 이 커널 파트에서 duplicate하지 않고 넘어가게 되는 것이므로
		// 나중에라도 또 할수있는거라고 이해했음!!
		return true;
	}
	
	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	void *for_child_page = palloc_get_page(PAL_USER); // child를 위한 새로운 page 만듦

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	
	memcpy(for_child_page, parent_page, PGSIZE); // parent page를 child page로 duplicate
	if (is_writable(pte)) {
		writable = true;
	} else {
		writable = false;
	} // parent page를 가리키는 포인터는 pte. writable하면 writable을 true로, 아니면 false로.
	
	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	// 아 걍 newpage가 child page였구나... ㅜ
	
	if (for_child_page != NULL) {
		newpage = for_child_page;
	} else {
		return false; // child page가 제대로 생성이 안된거임
	}
	

	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		palloc_free_page(newpage); // new page도 새로운 palloc을 한 것이므로 free 해줘야 함
		return false; // 당연히 fail하면 false 반환해야지
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if;
	bool succ = true;

	//printf("do fork는 출력됨?/n"); // 아니 안 출력됨...
	// 여기서 if_를 어떻게 넘겨줘야 하는건지 고민하라고 적혀있다.
	// 여기서, do fork로 들어가는 애는 자식인 것이다. 그러면, do fork 하기 전에
	// thread_current의 자신의 if를 저장해두면 된다.
	// 그니까 결국에 do fork 하기 전에 자기 자신의 if를 저장해 둔다음에
	// do fork를 하면 자식이 복제가 되는 거니까 그 상황에서 미리 저장해둔 자신의 (이제는 부모의) if를 받을 수 있는 것
	parent_if = &parent->if_for_fork;

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	// children process의 return값은 0이어야 함
	if_.R.rax = 0;

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		// 여기서, duplicate_pte를 썼기 때문에 일단은 child를 가리키는 pte는 생긴거임!
		// 그리고 페이지 세팅까지 끝난거다. 이제는 파일 내용만 복사하면 됨.
		// 위에서 kernel이더라도 true를 반환했을때, 이유가 여기 있다.
		// parent와 child에게 각각 pml4가 있지 않은 상태로 반환된거기 때문에 어차피 에러가 처리됨!
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/
	// exit status도 parent랑 똑같이 해줘야하나..?
	//current->exit_num = parent->exit_num;

	// 위 내용 요약) 파일 복제하려면 file_duplicate를 써라.
	// parent의 내용 모두 다 복사하기 전까지는 return하면 안된다.
	// 파일의 내용은 file_descriptor에 들어있다. 이내용들을 모두 복사하면 됨!
	///*
	struct list *parent_files = &parent->file_descriptor_table;
	struct list *child_files = &current->file_descriptor_table;
	for (struct list_elem *file = list_begin(parent_files); file != list_end(parent_files); file = list_next(file)) {
		// file이 비어있든 아니든 우리는 list 형태니까 그냥 다 복제해야 한다
		struct fd_structure *fd = list_entry(file, struct fd_structure, elem);
		struct file *real_file = file_duplicate(fd->current_file);
		// file이 null이던말던 fd만 null이 아니면, null 그대로 해놓고 다른 요소들 복제하면 됨
		if (fd == NULL) {
			goto error;
		} else {
			struct fd_structure *new_fd = calloc(1, sizeof(struct fd_structure));
			//struct fd_structure *new_fd = palloc_get_page(0);
			if (real_file == NULL) {
				// fd는 null이 아닌데 file이 null인 경우
				// 만약 child의 그 자리에 이미 파일이 있으면 어떡하지..?
				goto error;
			} else if (new_fd == NULL) {
				// calloc 한게 이상하면 또 error
				goto error;
			} else {
				// 정상인 경우
				new_fd->current_file = real_file;
				new_fd->fd_index = fd->fd_index;
				list_push_back(child_files, &(new_fd->elem)); // child list에 넣는다!
				//list_insert_ordered(child_files, &new_fd->elem, compare_fd_func, NULL);
			}
		}
	}
	//*/
	// curr fd도 똑같이 세팅
	current->curr_fd = parent->curr_fd;
	
	// 자식의 file 복사가 모두 끝났으므로 sema up, 그래서 이제 parent가 이어서 진행할 수 있음
	sema_up(&current->sema_for_fork);

	/* parent의 fork에 대한 sema를 추가한 이유: At Project 3
	아무리 해도 page-merge-par, stk, mm이 간헐적으로만 pass가 뜨고, 대부분 fail이 뜨는 것이었다.
	몇날며칠 밤을 새며 이유를 찾기 위해 노력했지만 계속 프린트를 하는데 진짜 이유를 모르겠는거임
	뜨는 오류는, the child is exit abnormally (like, exit(-1))에 대한 오류였다.
		load: child-qsort: open failed 가 뜨는 것이었다.
		(child-qsort) open "buf3": FAILED 라는 오류도 떴다.
	조금씩 디버깅 해보니, filesys_open할 때 inode가 NULL하다는 문제였다.
	근데 진짜 당최 왜 이런 오류가 뜨는건지를 모르겠는거다. 왜 어느때는 file이 있고 어느때는 file이 없냐..
	그냥 어렴풋하게 생각이 들었던 건 중간에 thread가 만들어질 때 file이 다 복사되기 전에
	또 다른 thread가 yield를 통해 running되어서 서로 간섭해서 오류가 생기나..라는 것이었다.
	게속 뭘 출력해보고 뭘 출력해보고 하다가 안되겠어서 피아자에 질문글을 올렸다.
	어느 한 분이 답글을 남겼다. "플젝3에서는 thread들이 모두 같은 priority를 가져요~"
	그렇다길래 내가 한 번 #ifdef VM 을 이용해서 플젝3에서는 tid 순서대로 ready list에 들어가게 했다.
	그러면 page-merge에서 뭔가 지들끼리 간섭 없이 잘 될 줄 알았다.
	하지만 당연하게도 장렬히 실패. page-merge 테케는 똑같은 문제가 나타났고,
	오히려 alarm부분의 테케들이 다 실패했다. 당연한거였지 하...
	이렇게까지 시도한 내 잔해들은 proj3_hany 브랜치에 고스란히 담겨있다.
	지금 거기서 make check 돌리면 거의 다 fail뜬다. 옛 commit으로 revert하면 ㄱㅊ을거임..

	그래서 일단 먼저 swap을 해결하고 난 다음에, 다시 이 피아자 질문글에 들어가보니까
	다른 분이 또 답글을 남겼더라. load 함수 안의 filesys_open() 전후에 lock 걸었다가 풀어보라고.
	그래서 해봤다. 그래도 오류는 똑같이 나고 추가적으로 page-parallel, swap-fork, syn-read&write가 fail하더라.
	모두 assertion lock_held_by_current_thread(lock)이 fail해서 PANIC이 온 것이었다.
	난 그냥 이때까지만 해도 page-merge는 똑같은 문제만 나오고 나머지가 더 fail이 나오니
	저렇게 lock 거는건 아예 상관이 없는거라고 생각했다. 그래서 또 의미없이 printf 디버깅만 하다가..
	저렇게 load 함수의 filesys_open에 lock이 걸려있는 상태로 몇번 더 page-merge 테케를 돌려보았다.
	아니 그랬더니 여기서도 assertion lock_held_by_current_thread(lock)이 fail하는 문제가 나왔다!
	한 7번정도 돌리면 1번은 성공, 5번은 위의 원래 오류들, 그리고 다른 1번이 이 lock assertion이 뜨는 것이었다.
	아니 진짜 어이없었던 건, 정말 filesys_open 직전에 lock_acquire을 적고, 직후에 lock_release를 적었는데
	이 사이에 왜 이 assertion이 fail이 뜨는것인가? 아무리 생각해도 말이 안되는 것이었다.
	진짜 그렇다면 filesys_open하는 동안에 lock을 걸어놨음에도 불구하고 thread current가 바뀌거나
	lock holder이 바뀌어서 저 오류가 발생하는 것 아니겠는가. 아니 저 짧은 시간에?
	그래서 바로 프린트 해봤다. 아 나는 tid를 이용해서 몇 번째 chunk인지 알아냈다. tid-4가 chunk num이더라.
	즉, printf("chunk %d\n", thread->tid-4) 이렇게 출력했다는 말.
	그래서 lock_acquire과 filesys_open 사이에(bf open) thread_current와 lock->holder을 출력하고,
	filesys_open과 lock_release 사이에(af open) thread_current와 lock->holder을 출력해봤다.
	정말 간헐적으로 그 lock assert가 떴고, 이때 확인해보니
	-----원래라면----
	(bf open) current thread: chunk 3
	(bf open) lock holder: chunk 3
	(af open) current thread: chunk 3
	(af open) lock holder: chunk 3
	----------------
	-----하지만 lock assertion 오류 시에는-----
	(bf open) current thread: chunk 2
	(bf open) lock holder: chunk 2
	(bf open) current thread: chunk 3
	(bf open) lock holder: chunk 3
	(af open) current thread: chunk 2
	(af open) lock holder: chunk 3
	------------------------------------------
	이렇게 뜨는 것이었다. 즉, 오류 시에는 lock_acquire을 하고 갑자기 한 번 더 또 lock_acquire을 한 것이었다.
	이게 말이 되는가!!! 나는 lock을 걸어놨는데!!!!
	그래서 이렇게 지혼자 툭 튀어나오는 경우는 fork밖에 없다고 생각했다. 그래서 출력해봤더니,
	------------------------------------------
	(page-merge-stk) sort chunk 1
	(bf open) current thread: chunk 0
	(bf open) lock holder: chunk 0
	혹시 fork 때문에 바뀌는 건가?
	(fork) parent's chunk -1, child's chunk 1
	(bf open) current thread: chunk 1
	(bf open) lock holder: chunk 1
	(af open) current thread: chunk 0
	(af open) lock holder: chunk 1
	------------------------------------------
	이런 식으로 뜨는 것이었다. fork시에 parent는 chunk -1이라고 나와있지만 얘는 (page-merge-stk)인,
	즉 chunk라는 자식을 계속 만들어내는 parent인 것이다. 몇번씩 더 돌려보니까,
	저 fork의 parent는 안 변하고(당연) 계속 chunk 자식들을 만들어내는데, 그 시기가 아주 들쭉날쭉하시다.
	내가 chunk 0~ 과 같은 애들을 lock으로 막아놔봤자 뭐해, parent는 lock으로 구애 안 받고 열심히 child나 fork하고 다니시는데!!!
	이걸 그제서야 깨달은 것이다. 하... fork가 들쭉날쭉한 타이밍에 되는 걸 보고,
	내가 지금까지 fork를 제어를 안했었던가 되돌아보게 되었다. 그래서 process_fork를 확인해보니,
	내가 fork_sema를 걸어준 대상은 child 뿐이었다. 이 미친뇬.
	근데 플젝2를 포함해서 지금까지 테케들이 통과된 것도 신기하다. ㅋㅋ
	결국에는 fork하는 동안에는 parent도 다른 허튼 짓 못하도록 sema를 이용해서
	그 행위를 막아줘야 했던 것이었다...
	즉, fork를 해서 child를 만들어냈으면, 그때부터 ~ child가 행동을 다 끝내기 전까지
	parent가 fork를 더 하는 그런 허튼 행위를 못하도록 해야하는 것이었다.
	결국엔 이 문제 떄문에, parent가 child를 만들어내놓고 그 child가 뭐 file 복사하고
	뭐하고 다하고 종료되는 동안을 기다려주지 못하고 또 child를 만들어내버리니
	child끼리 서로 간섭하면서 지들끼리 file없고 lock 가로채고 그런 것이었다...
	그래서 fork가 끝나고, parent의 sema_fork를 down 시켜준 뒤 child가 다 끝날 때까지
	wait하고, child가 다 끝나면 parent의 sema_fork를 up 시켜줘서 또 다른 child를 fork할 수 있게 했다
	와 그랬더니 그냥 마법처럼 page-merge-par, stk, mm이 해결되는 것이었다. 진짜 눈물날 뻔 했다.
	여기까지의 모든 시도들은 solve_merge branch에서 확인 가능하다.
	fork가 문제라는 걸 알기 전에 syscall.c의 모든 file관련 함수들 lock 다시 재배치도 해보고,
	다른것들도 막 건드려보고, process.c에 있는 load를 제외한 다른 함수들 중에 file관련 함수들 쓰는
	애들도 다 lock 걸어주고, load 안에서도 file함수 주변은 다 lock 걸어주고... 뭐 여기도 총체적 난국이다.
	그래도 여기는 테케 다 통과됨 우하하~
	지금까지 page-merge-par, stk, mm을 해결하기 위해 밤도 계속 새웠던 어느 os 응애의 이야기였습니다. */
	sema_down(&parent->sema_for_fork); // 결국 이 parent->sema_for_fork가 살린 것이다.
	process_init ();

	/* Finally, switch to the newly created process. */
	if (succ)
		// parent의 child list에 본인을 추가해줘야 한다!
		//list_push_back(&parent->my_child, &current->my_child_elem); // 두 번이나 추가해주는 꼴인가?
		do_iret (&if_); 
error:
	// 에러가 나도 일단 sema는 up해야. 안그러면 더이상 안돌아가잖아...
	current->exit_num = TID_ERROR;
	sema_up(&current->sema_for_fork);
	sema_down(&parent->sema_for_fork); // 결국 이 parent->sema_for_fork가 살린 것이다.
	thread_exit ();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	//command line 문자열을 f_name으로 받는다
	//char*로 변환을 해야 이를 문자열로 인식할 수 있다
	char *file_name = f_name;
	bool success;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	//user프로그램을 실행할때 필요한 정보를 포함 - stores the registers of the user space
	//interrupt frame은 인터럽트가 호출되었을때 이전에 레지스터에 작업하던 context 정보를 스택에 담는 구조체
	//커널모드에서 유저모드로 바뀔때 결국 interrupt가 발생하기 때문에 유저스택을 가지고 오기 위해 커널 스택내에서 이 intr frame을 만들어서 받아와야한다
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	//현재 프로세스에 할당된 page directory를 지운다
	process_cleanup ();
	#ifdef VM
		supplemental_page_table_init(&thread_current()->spt);
	#endif

	//command line을 파싱해서 들어오는 argument들을 찾고 어딘가에 보관해놔야한다
	//pointer 형태로 각 argument를 저장해놓는다
	//in C, we use strtok function to split a string into a series of tokens based on a particular delimiter
	//char *strtok(char *str, const char *delim) 헤더 형태를 사용해서 들어온 command line을 space를 delimiter로 word단위로 쪼갠다
	//하지만 strtok을 사용하면 original string도 바꾸게 된다 --> file_name를 다른 variable에 복사해놓자
	//.io에서 command line arguments에는 128 바이트의 제한이 있다고 써있기 때문에 parameter을 저장하는 공간을 128 바이트만 allocate
	char *command_line_args[64];
	//char * f_name_copy[64];
	
	//C언어에서 string 복사하는거는 memcpy 함수 사용한다, strlen을 하면 \n을 포함하지 못하니까 +1까지 복사해야한다.
	//memcpy(void * ptr, int value, size_t num)이라 ptr에서 value까지의 바이트 길이
	//memcpy(f_name_copy, file_name, strlen(file_name) + 1);
	//space가 여러개 있는거와 하나 있는거랑 상관없이 다 제거해야함
	const char delimeter[] = " ";
	//첫 word는 program name이다
	//strtok을 통해서 얻는 정보는 command line의 각 단어의 위치 정보이다!!! 실제 string을 가지고 있는게 아니라 그 address를 담고 있다
	//strtok_r을 하면 공백을 찾으면 이 자리에 null sentinel을 대신 넣어주게 된다 - 우리가 원하는 바 
	char * save;
	char * word_tokens = strtok_r(file_name, delimeter, &save);
	char * program_name = word_tokens;
	//command_line_args는 이 단어들의 위치정보를 array형태로 모아둔곳이다. 결국에 command_line_args[i]는 각 단어의 위치정보이기때문에 단어 자체는 아니다.
	command_line_args[0] = program_name;
	int parameter_index = 0;

	//strtok이 NULL을 내뱉을때까지 loop안에서 토큰을 찾아야한다
	//char * word_tokens;
	while (word_tokens != NULL) {
		parameter_index++;
		word_tokens = strtok_r(NULL, delimeter, &save);
		command_line_args[parameter_index] = word_tokens;
	}

	/* And then load the binary */
	//_if와 file_name을 현재 프로세스에 로드한다 (성공: 1, 실패: 0)
	//load 함수의 설명: Stores the executable's entry point into *RIP and its initial stack pointer into *RSP
	success = load (program_name, &_if);

	// load를 하자마자 success 여부를 판단해야 한다. (exec-missing test에서 뜬 에러)
	// 그러지 않으면, strlcpy(frame -> rsp, command_line_args[i], strlen(command_line_args[i]) + 1);
	// 부분에서 page fault 에러가 뜬다! program_name이 이상하면 rsp도 이상하니까,
	// 위에서 copy하면서 (~while까지) command_line_args[i]의 부분들이 그럼 null이 되니까.
	if (!success)
	 {
		palloc_free_page (file_name);
		return -1;
	 }

	//가장 마지막으로 추가된 parameter부터 푸시가 되어야하니까 현재 command_line_args 포인터가 포인트하고 있는게 마지막으로 추가된 위치 + 1일것이다
	//char *last_elem_index = command_line_args - 1;
	//int로 카운트를 따라야하기때문에 parameter_index를 사용해서 오른쪽부터 왼쪽까지 iterate한다

	//유저가 요청한 프로세스를 수행하기 위한 interrupt frame 구조체 내 정보를 유저 커널 스택에 쌓는다
	//단어 데이터를 저장 해놓기 (order 상관없으니 그냥 첫 단어부터 넣기로 하자)
	//null pointer sentinel도 있기 때문에 parameter_index부터 시작해서 strlen + 1만큼 loop해야한다
	struct intr_frame * frame = &_if;
	//command_line_args의 첫 element의 주소를 가르키게된다
	char **words = command_line_args;
	//오른쪽 (가장 마지막 parameter)부터 왼쪽 방향으로 스택에 push address of each string plus a null pointer sentinel (\0)
	//'bar\0' 이런식으로 저장해줘야한다
	char *word_stack_address[64];

	for (int i = parameter_index - 1; i >= 0; i--) { 
		//1. top of the stack에다가 각 단어들을 넣어줘야하고 이때 스택은 밑으로 grow한다 -- 스택 포인터가 커진다?
		//x86-64에서는 %rsp 레지스터가 현재 스택의 가장 낮은 주소(Top의 주소)를 저장
		//(pointer) -> (variable)으로 하면 포인터가 가르키고 있는 variable의 실제 정보를 가져온다
		//void * stack_pointer = frame -> rsp;

		//현재 command_line_args는 각 단어의 address를 담고 있는 포인터이고 우리는 단어 데이터 자체를 넣어주려고 하기 때문에
		//words[i]는 ith argument의 정보를 담고 있는 포인터이다
		//char** actual_word = words[i];
		//void *: pointer to any data type
		
		//strlen에서 assertion 'string' failed 에러가 난다 -- 즉 words에 뭐가 안 담겨져 들어가는거 아닌가
		frame -> rsp -= (strlen(command_line_args[i]) + 1);	
		//스택에 자리를 만들어줘야 푸시가 가능하기때문에 스택 포인터를 밑으로 늘린다
		//rsp가 가르키는 공간에다가 찾은 단어를 넣어줘야하기 때문에 실제 공간에 접근하고 단어를 단어의 길이만큼 만들어진 스택 공간에 넣어준다
		//strcpy를 사용해서 src string을 dest에 복사해서 넣어주기 때문에 이걸 사용하자
		strlcpy(frame -> rsp, command_line_args[i], strlen(command_line_args[i]) + 1);

		//이 단어가 어느 스택주소에 저장되어있는지를 나중에 또 넣어줘야하기때문에 현재 rsp가 가르키는 주소를 저장해줘야한다
		word_stack_address[i] = frame -> rsp;
	}
	//데이터를 Push 할 때는 %rsp의 값을 8만큼 감소시켜야 한다
	//round the stack pointer down to a multiple of 8 before the first push
	//void * current_rsp = _if.rsp;
	while (frame -> rsp % 8 != 0) {
		//printf("inside here");
		frame -> rsp = frame->rsp - 1;
		*(uint8_t *)frame -> rsp = 0;	//rsp가 가르키고 있는 공간에 0으로 채워넣는다
	}

	//null pointer sentinel 0을 char * 타입으로 스택에 푸시해줘야한다 (null terminating \0)
	frame -> rsp -= 8;
	//*current_rsp로 스택 포인터가 가르키고있는 실제 공간/데이터에 char * 인 0을 넣어줘야한다.
	//0은 int타입으로 인식되기 때문에 NULL을 넣어놓는다 - 따라서 현재 이 위치에는 null pointer를 넣어준다
	memset(frame -> rsp, 0, sizeof(char **));
	//printf("added null pointer");

	//이제 마지막 파라미터부터 시작해서 각 단어들이 지금 저장되어있는 위치를 스택에 추가한다
	for (int i = parameter_index - 1; i >= 0; i--) {
		//printf("loop for adding address");
		frame -> rsp -= 8;
		memcpy(frame -> rsp, &word_stack_address[i], sizeof(char *));
	} 

	//지금 순차적으로 addresss를 스택에 넣어줬기때문에 지금 current_stack_pointer가 결국에 argv[0]의 포인터를 담고 있다
	//command_line의 첫 단어는 program name을 담고 있고 이를 가르키는 포인터를 %rsi에 저장해놓는다
	frame -> R.rsi = frame -> rsp;
	frame -> R.rdi = parameter_index;

	//fake return address를 마지막으로 푸시해야하기때문에 그냥 0을 넣는다 -- 타임은 void (*) ()
	frame -> rsp -= 8;
	memset(frame -> rsp, 0, sizeof(void *));

	//hex_dump(_if.rsp , _if.rsp , USER_STACK - (uint64_t)_if.rsp, true);

	/* If load failed, quit. */
	//load를 끝내면 해당 메모리를 반환해야 한다
	palloc_free_page (file_name);

	if (!success)
		return -1;

	/* Start switched process. */
	//do_iret을 통해서 실제로 사용자 프로세스로 넘어가게 된다 
	do_iret (&_if);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
	//bool temporary = true;
	//for (int i = 0; i < 1000000000; i++) {}
	///*
	//printf("child tid: %d\n", child_tid);
	//printf("왜그래\n");
	struct thread *parent = thread_current();
	//printf("cur thread name: %s\n", parent->name); // main
	// fork에서 썼던거 그대로 가져와도 될듯!!
	struct list *children = &parent->my_child;
	//printf("list empty?: %d\n", list_empty(children)); // main의 children list는 비어있음
	if (list_empty(children)) {
		return -1;
	}
	struct list_elem *child;
	//printf("왜그래\n");
	//printf("parent thread name: %s\n", parent->name);
	//printf("list size: %d\n", (int) list_size(children)); // 왜 2지?? thread.c에서 확인해 본 결과 만들때부터 1이 되는가봄.
	for (child = list_begin(children); child != list_end(children); child = list_next(child)) {
		//printf("child name: %s\n", list_entry(child, struct thread, my_child_elem)->name);
		if (child_tid == list_entry(child, struct thread, my_child_elem)->tid) {
			break;
		}
	}
	//printf("왜그래\n");
	if (child == list_end(children)) {
		//printf("여긴가\n"); // 여기임. 끝까지 갔으면 음 그냥 sema up 시켜줘야 하는거?
		return -1;
	}
	//printf("왜그래\n");
	struct thread *real_child = list_entry(child, struct thread, my_child_elem);
	//ASSERT(real_child == NULL); // 이렇게 real_child가 null이면... 일단 fork할 때 list에 안들어가진건가
	//printf("real_child name: %s\n", real_child->name);
	//printf("real_child tid: %d\n", real_child->tid);
	if (real_child == NULL) {
		return -1;
	}
	sema_up(&parent->sema_for_fork); // 결국 이 parent->sema_for_fork가 살린 것이다. 이유는 __do_fork에서.
	sema_down(&real_child->sema_for_wait); // parent가 wait하다가
	// 그러면 여기서 child가 열심히 돌아가다가 이제 exit 될거고,
	// exit 되면 sema up을 하면서 자신의 exit_num이 나올 것!
	int child_exit_num = real_child->exit_num;
	//printf("child exit num: %d\n", child_exit_num); // 아니 근데 이거 원래 출력 안 됐는데 갑자기 되는 이유좀???
	// 앞에서 list_push_front로 바꾸고 나서부터인가....
	list_remove(&real_child->my_child_elem); // child는 일 다 끝났으니까 리스트에서 지움
	//printf("list size: %d\n", list_size(children)); // 왜 1이지??
	sema_up(&real_child->sema_for_exit);

	return child_exit_num;
	//*/
	//return -1;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	
	//printf("여기는 출력 되나\n"); // 안되네...

	// termination message를 확인해보니
	/* (exit) begin
		exit: exit(57) */
	// 이런 식으로 뜬다. 그러면 exit 숫자를 그대로 적으면 되는건가!
	//printf("%s: exit(%d)\n", curr->name, curr->exit_num);
	// 아니 이렇게 적으면 project 1에서도 저 exit가 뜸... 아니 여기서 저 print 적으라매 진짜 너무해
	//printf("file이 뭘까: %d\n", curr->executing_file);
	if (curr->executing_file != NULL) {
		//printf("어디야1\n");
		file_allow_write(curr->executing_file); // 쓸 수 있게 해준 뒤
		file_close(curr->executing_file); // 삭제해야함!
		curr->executing_file = NULL;
		palloc_free_page(curr->executing_file);
	}
	//file_close(curr->executing_file);

	// 현 thread에 있는 모든 파일들을 닫아줘야 함
	struct list *file_list = &curr->file_descriptor_table;
	while (!list_empty(file_list)) {
		//printf("어디야2\n");
		struct fd_structure *file = list_entry(list_pop_front(file_list), struct fd_structure, elem);
		file_close(file->current_file);
		//palloc_free_page(file);
		free(file);
	}

	// child list에 있는 애들도 모두 없애줘야 함. 고아가 될 수는 없잖아!
	struct list_elem *dont_be_orphan = list_begin(&curr->my_child);
	while (!list_empty(&curr->my_child)) {
		//printf("어디야3\n");
		dont_be_orphan = list_remove(dont_be_orphan);
	}

	process_cleanup ();

	// child가 exit하면, wait sema를 올려준다!
	sema_up(&curr->sema_for_wait);
	// 부모가 exit 할 때까지 기다린다
	sema_down(&curr->sema_for_exit);

	#ifdef FILESYS
		dir_close(thread_current()->current_dir);
	#endif
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	//현재 쓰레드의 pml4 생성 및 활성화 시킨다 
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Read and verify executable header. */
	//오픈한 파일을 읽는다 - elf 헤더의 크기만큼 ehdr에 읽고 정상적인지 확인
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	//
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {	//phdr 하나씩 순회하면서 type이 PT_LOAD이면 로드 가능한 세그먼트라는것을 표시한다
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {	//phdr 각각은 세그먼트에 대한 정보를 가진다
					bool writable = (phdr.p_flags & PF_W) != 0;	
					uint64_t file_page = phdr.p_offset & ~PGMASK;	//file page 초기화
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;		//memory page 초기화 (밑 12 바이트 버린다)
					uint64_t page_offset = phdr.p_vaddr & PGMASK;	//page offset 초기화 - read_bytes, zero_bytes 계산에 쓰인다
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */
   
	//오픈된 파일에는 write가 일어나지 않도록 deny file write을 해줘야한다
	if (file != NULL) {
		file_deny_write(file);
		//deny write으로 막아놓은 다음에 파일을 실행시킬수있도록
		t -> executing_file = file;
	}
	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	//file_close (file); // 여기서 말고 exit할 때 닫게 해야함
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	//이전과 달리 segment를 physical frame에 직접 할당하는 방식 말고 
	//spt에 필요한 정보들을 넣어놓고 page fault가 발생했을떄 (즉, 페이지가 요청될때)가 되어야 메모리에 load하는 방식
	//첫번째 page fault가 발생하면 vm_do_claim_page에서 매핑이 이루어진 이후에 uninit_initialize함수가 호출되고
	//그 안에서 각각 페이지 타입 별 초기화 함수와 내용을 로드하는 함수 (lazy load segment)가 호출된다
	//여기서는 내용만 물리 프레임에 로딩하는 작업만 하면된다
	bool load_done = true;
	struct segment_info *load_info = (struct segment_info *)aux;

	//find the file to read the segment from and read segment into memory
	struct file *file = load_info->page_file;
	off_t offset = load_info->offset;
	size_t page_read_bytes = load_info->read_bytes;
	size_t page_zero_bytes = load_info->zero_bytes;
	void *buffer = page->frame->kva;
	//파일 위치를 찾아야한다
	file_seek(file, offset);
	//offset에 담긴 파일을 물리 프레임으로부터 읽어야하기 때문에 page의 frame에 접근해서 kernel의 주소를 사용해서 읽는다
	off_t read_info = file_read(file, buffer, page_read_bytes);
	if (read_info != (int) page_read_bytes) {
		palloc_free_page(buffer);
		load_done = false;
	} else {
		memset(buffer + page_read_bytes, 0, page_zero_bytes);
	}

	return load_done;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		//넘겨줘야하는 내용은 file, offset, read_bytes, zero_bytes이렇게 넘겨줘야하기 때문에 아예 structure으로 만들어서 넘겨주도록 하자
		struct segment_info *load_info = (struct segment_info *)malloc(sizeof (struct segment_info));
		load_info->page_file = file;
		load_info->offset = ofs;
		load_info->read_bytes = page_read_bytes;
		load_info->zero_bytes = page_zero_bytes;
		//vm_alloc_page 함수를 호출해서 페이지를 생성해주는거다
		//여기서 5번째 인자인 aux가 페이지에 로드할 내용이고 4번째 인자인 lazy_load_segment가 이 내용물을 넣어주는 함수이다
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, load_info))
			//이 과정에서 lazy_load_segment을 호출한뒤 반환값을 vm_alloc의 인자로 넣는다
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	//stack은 아래로 커지기 때문에 stack의 시작점인 USER_STACK에서 페이지 사이즈만큼 아래로 내린 지점을 이 stack_bottom으로 하고 여기에 페이지를 생성한다
	//stack 역시 하나의 anonymous 페이지라고 생각할 수 있다
	//user program에서 함수가 호출되면 리턴값이 쌓이게 되는데 이때 스택 페이지에 접근해서 해당 내용을 적는다
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */
	//초기화된 UNINIT한 페이지를 하나 생성
	/*
	bool page_init = vm_alloc_page_with_initializer(VM_ANON | VM_MARKER_STACK, stack_bottom, true, NULL, NULL);
	*/
	bool page_init = vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, true);
	if (page_init) {
		//할당받은 페이지에 바로 물리 프레임을 매핑해준다
		//스택은 바로 사용하기 때문에 어차피 써야할 페이지에 대해서 물리 프레임을 연결해주는 함수이다
		bool check = vm_claim_page(stack_bottom);
		//ASSERT(check == true);
		//ASSERT(check == false);
		if (check) {
		/*
		if (vm_claim_page(stack_bottom)) {
			*/
			/* 여기 vm_claim_page(stack_bottom)이 true니까 여기로 들어온 거 아니냐? */
			//ASSERT(!vm_claim_page(stack_bottom));
			//ASSERT(!check);
			//페이지가 스택이라고 마크해줘야한다
			if_->rsp = USER_STACK;
			//thread_current()->user_stack_rsp = stack_bottom;
			success = true;
			//ASSERT(0);
			ASSERT(is_user_vaddr(stack_bottom));
			ASSERT(!is_kernel_vaddr(stack_bottom));
			//memset(stack_bottom, 0, PGSIZE); // 지금 여기서부터 handler fault가 난다.
			if_->rsp = USER_STACK;
		} else {
			// 만약 claim page가 실패했다면,
			struct page *have_to_free_page = spt_find_page(&thread_current()->spt, stack_bottom);
			palloc_free_page(have_to_free_page);
			return success;
		}
	}

	return success;
}
#endif /* VM */
