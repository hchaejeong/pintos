#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// 여기서 이제, include/lib/syscall-nr.h를 보면 각 경우의 system call #을 알 수 있다.
	// 그리고, 여기서부터는 레지스터를 사용해야 한다. 이건 include/threads/interrupt.h에 있다!
	// intr_frame 안에는 register 모임?인 R이 있고, R 안에는 깃헙 io링크에 있는 %rax 이런애들이 다 있다!

	switch (f->R.rax) {
		// %rax는 system call number이라고 적혀있다
		// include/lib/user/syscall.h에는 구현해야할 모든 경우?가 다 적혀있다.
		// 레지스터의 순서는 %rdi, %rsi, %rdx, %r10, %r8, %r9임!
		uint64_t arg1 = f->R.rdi;
		uint64_t arg2 = f->R.rsi;
		uint64_t arg3 = f->R.rdx;
		case (SYS_HALT):
			halt();
			break;
		case (SYS_EXIT):
			exit(arg1);
			break;
		case (SYS_FORK):
			f->R.rax = fork(arg1, f);
			break;
		case (SYS_EXEC):
			exec(arg1);
			break;
		case (SYS_WAIT):
			f->R.rax = wait(arg1);
			break;
		case (SYS_CREATE):
			f->R.rax = create(arg1);
			break;
		case (SYS_REMOVE):
			f->R.rax = remove(arg1);
			break;
		case (SYS_OPEN):
			f->R.rax = open(arg1);
			break;
		case (SYS_FILESIZE):
			f->R.rax = filesize(arg1);
			break;
		case (SYS_READ):
			f->R.rax = read(arg1);
			break;
		case (SYS_WRITE):
			f->R.rax = write(arg1);
			break;
		case (SYS_SEEK):
			seek(arg1);
			break;
		case (SYS_TELL):
			f -> R.rax = tell(arg1);
			break;
		case (SYS_CLOSE):
			close(arg1);
			break;
		default:
			printf ("system call!\n");
			thread_exit ();
	}
	// printf ("system call!\n");
	// thread_exit ();
}

void
halt(void) {
	power_off();
}

void
exit (int status) {
	thread_exit();
}

tid_t
fork (const char *thread_name, struct intr_frame *f) {
	return process_fork(thread_name, f);
}

int
exec (const char *file) {
	return 0;
}

int
wait(tid_t pid) {
	return process_wait(pid);
}