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
	return thread_create (name,
			PRI_DEFAULT, __do_fork, thread_current ());
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

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
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

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));

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
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	process_init ();

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
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
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	//현재 프로세스에 할당된 page directory를 지운다
	process_cleanup ();

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

	//가장 마지막으로 추가된 parameter부터 푸시가 되어야하니까 현재 command_line_args 포인터가 포인트하고 있는게 마지막으로 추가된 위치 + 1일것이다
	//char *last_elem_index = command_line_args - 1;
	//int로 카운트를 따라야하기때문에 parameter_index를 사용해서 오른쪽부터 왼쪽까지 iterate한다

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

	hex_dump(_if.rsp , _if.rsp , USER_STACK - (uint64_t)_if.rsp, true);

	/* If load failed, quit. */
	//load를 끝내면 해당 메모리를 반환해야 한다
	palloc_free_page (file_name);

	if (!success)
		return -1;

	/* Start switched process. */
	//load가 실행되면 context switching을 시킨다
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
	for (int i = 0; i < 1000000000; i++) {

	}
	return -1;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */

	process_cleanup ();
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
	

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	file_close (file);
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
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */
