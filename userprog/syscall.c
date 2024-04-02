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
	
	//여러 프로세스가 같은 파일을 사용하지 않도록 락을 걸어놓자
	lock_init(&file_lock);
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
			f->R.rax = fork(arg1);
			break;
		case (SYS_EXEC):
			exec(arg1);
			break;
		case (SYS_WAIT):
			f->R.rax = wait(arg1);
			break;
		case (SYS_CREATE):
			f->R.rax = create(arg1, arg2);
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
			f->R.rax = read(arg1, arg2, arg3);
			break;
		case (SYS_WRITE):
			f->R.rax = write(arg1, arg2, arg3);
			break;
		case (SYS_SEEK):
			seek(arg1, arg2);
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

//Creates a new file called file initially initial_size bytes in size
//파일을 만들뿐, 파일 여는것은 open 함수에서 한다
bool 
create (const char * file, unsigned initial_size) {
	//일단 항상 현재 들어오는 파일 주소가 유효한지 체크를 한 후에 넘어가야한다
	//check_bad_ptr(file);
	//file descriptor table을 할당해줘야하니까 현재 진행중인 프로세스/쓰레드를 받아와서 진행해야함
	struct thread * current_thread = thread_current();

	//포인터를 지금 만들어준 file descriptor table로 지정해줘야함
	//파일테이블에서의 fd0, fd1은 stdin, stdout으로 세이브해놓을거때문에 2부터 실제 파일을 넣어주도록

	//synchronization을 위해서 lock_acquire를 통해 다른 프로세스 접근 못하게 막아놓은 다음에 파일은 만들어야한다
	lock_acquire(&file_lock);
	//이 filesys_create 함수가 파일이 성공적으로 생성될때만 true한 값을 내뱉기때문에 사실상 이게 결국에 이 시스템콜에서 리턴해야할 값이다
	bool status = filesys_create(file, initial_size);
	//lock_release로 다시 다른 프로세스도 사용가능하도록 풀어준다
	lock_release(&file_lock);

	return status;
}

//파일을 제거하는 함수 - open되어 있는 파일은 닫지 않고 그대로 그냥 켜진 상태로 남아있게된다
bool
remove (const char *file) {
	//create랑 같은 로직인데 그냥 filesys_remove함수를 사용한다
	lock_acquire(&file_lock);
	bool status = filesys_remove(file);
	lock_release(&file_lock);

	return status;
}

//Opens the file called file. 
//Returns a nonnegative integer  called a "file descriptor" (fd), 
//or -1 if the file could not be opened.
int
open (const char * file) {
	//fd0, fd1은 stdin, stdout을 위해 따로 빼둬야한다 -- 0이나 1을 리턴하는 경우는 없어야한다
	lock_acquire(&file_lock);

	struct thread *current_thread = thread_current();

	struct list *curr_fdt = &current_thread -> file_descriptor_table;
	bool fdt_empty = false;
	struct fd_structure *fd_elem = calloc(1, sizeof(struct fd_structure));
	if (fd_elem != NULL) {
		if (list_empty(curr_fdt)) {
			fd_elem->fd_index = 2;
			fdt_empty = true;
		}
	} else {
		return -1;		//새로운 파일 열 공간이 부족한 경우 open되지 않기 때문에 -1을 리턴한다.
	}

	
	struct file *actual_file = filesys_open(file);
	if (!actual_file) {		//파일을 열지 못한 경우
		free (fd_elem);		//할당한 공간을 사용하지 않았기 때문에 다시 free해줘서 다른 애들이 쓸 수 있게 해준다.
		lock_release(&file_lock);
		return -1;
	}

	//파일이 제대로 잘 열렸으면 지금 fd 원소의 파일에 저장해놓는다
	fd_elem->current_file = actual_file;
	//각 프로세스마다 자신만의 file descriptors을 가지고 있다 - child processes
	//현재 fdt 리스트의 맨 뒤에 넣기 때문에 현재 최대 index보다 1을 increment해줘야한다.
	if (!fdt_empty) {
		int prev_index = list_entry(list_back(curr_fdt), struct fd_structure, elem)->fd_index;
		fd_elem->fd_index = prev_index + 1;
	}
	//파일을 새로 열때마다 리스트에다가 추가해줘야하니까 맨 끝에 푸시를 해준다
	struct list_elem curr_elem = fd_elem->elem;
	list_push_back(curr_fdt, &curr_elem);


	lock_release(&file_lock);

	//lock release한 다음에 결과를 반환해줘야한다
	return fd_elem->fd_index;
}

//off_t file_length(struct file *file)함수를 사용한다
//fd에 있는 파일의 사이즈를 반환
int
filesize (int fd) {
	int size = 0;
	lock_acquire(&file_lock);

	//저 file_length함수를 쓰기 위해서는 struct file *형태인 주어진 fd에 있는 파일을 가져와서 이거에 저 함수를 돌려야된다
	//따라서 우리 파일 테이블 리스트에서 fd 위치에 있는 것을 뽑아와야한다 -- 이걸 해주는 새로운 함수를 만들자
	struct fd_structure *fd_elem = find_by_fd_index(fd);
	if (fd_elem == NULL) {
		//찾지 못한거기때문에 -1을 반환시킨다
		size = -1;
	} else {
		size = file_length(fd_elem->current_file);
	}

	lock_release(&file_lock);

	return size;
}

int
read (int fd, void *buffer, unsigned size) {

}

int
write (int fd, const void *buffer, unsigned size) {

}

void
seek (int fd, unsigned position) {

}

unsigned
tell (int fd) {

}

//close file descriptor fd
void
close (int fd) {
	lock_acquire(&file_lock);

	


	lock_release(&file_lock);
}

struct fd_structure*
find_by_fd_index(int fd) {
	lock_acquire(&file_lock);

	struct thread *current_thread = thread_current();
	struct list *curr_fdt = &current_thread -> file_descriptor_table;
	struct fd_structure *curr_fd_elem = list_entry(list_front(curr_fdt), struct fd_structure, elem);
	while (curr_fd_elem != list_end(curr_fdt)) {
		if (curr_fd_elem -> fd_index == fd) {
			//그럼 이 fd원소가 찾아진거기때문에 이 fd_structure element를 반환한다
			return curr_fd_elem;
		}

		curr_fd_elem = list_next(curr_fdt);
	}

	//다 찾아봤는데 없으면 NULL을 반환하도록
	return NULL;
}