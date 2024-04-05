#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <stdlib.h>
#include "threads/synch.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "intrinsic.h"
#include "userprog/process.h"

struct lock file_lock;

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void halt(void);
bool create (const char * file, unsigned initial_size);
bool remove (const char *file);
int open (const char * file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
struct fd_structure* find_by_fd_index(int fd);
void exit(int status);
tid_t fork (const char *thread_name, struct intr_frame *f);
int exec (const char *file);
int wait(tid_t pid);

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
			f->R.rax = fork((char *) arg1, f);
			break;
		case (SYS_EXEC):
			f->R.rax = exec((char *) arg1);
			break;
		case (SYS_WAIT):
			f->R.rax = wait((tid_t) arg1);
			break;
		case (SYS_CREATE):
			f->R.rax = create((char *) arg1, (unsigned) arg2);
			break;
		case (SYS_REMOVE):
			f->R.rax = remove((char *) arg1);
			break;
		case (SYS_OPEN):
			f->R.rax = open((char *) arg1);
			break;
		case (SYS_FILESIZE):
			f->R.rax = filesize((int) arg1);
			break;
		case (SYS_READ):
			f->R.rax = read((int) arg1, (void *) arg2, (unsigned) arg3);
			break;
		case (SYS_WRITE):
			f->R.rax = write((int) arg1, (void *) arg2, (unsigned) arg3);
			break;
		case (SYS_SEEK):
			seek((int) arg1, (unsigned) arg2);
			break;
		case (SYS_TELL):
			f -> R.rax = tell((int)arg1);
			break;
		case (SYS_CLOSE):
			close((int) arg1);
			break;
		default:
			printf ("system call!\n");
			thread_exit ();
	}
	// printf ("system call!\n");
	// thread_exit ();
}

// lock_acquire할때 현재 돌아가고 있는 쓰레드가 락을 이미 가지고 있는데 요청한거면
// 필요없으니 
void 
check_address(void *address) {
	if (address >= LOADER_PHYS_BASE && lock_held_by_current_thread(&file_lock)) {
		lock_release(&file_lock);
	}
	exit(-1);
	NOT_REACHED();
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
	check_address(file);

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
	check_address(file);
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
	check_address(file);
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

//fd의 파일에서 size bytes을 읽고 buffer에 넣는다
//실제로 읽은 byte 사이즈를 반환하고 파일을 읽지 못한 경우에는 -1을 반환시킨다
int
read (int fd, void *buffer, unsigned size) {
	check_address(buffer);
	check_address(buffer + size - 1);

	int read_bytes = 0;
	lock_acquire(&file_lock);

	//fd가 0이면 파일에서 읽지 않고 keyboard에서 input_getc()로 input을 읽어야한다
	if (fd == 0) {
		uint8_t key = input_getc();	 //user가 입력하도록 기다리고 입력하는 키보드 key를 반환한다

	}

	struct fd_structure *fd_elem = find_by_fd_index(fd);
	if (fd_elem == NULL) {
		read_bytes = -1;
	} else {
		//지금 fd에 맞는 파일을 뽑아오고 이 파일을 읽어줘야한다
		struct file *curr_file = fd_elem->current_file;
		read_bytes = (int) file_read(curr_file, buffer, size);
	}

	lock_release(&file_lock);

	return read_bytes;
}

//buffer에서 size bytes를 fd의 파일에다가 쓰는 함수
//실제로 써지는 byte만큼을 반환한다 - 안 써지는 byte들도 있을수 있기 때문에 size 보다 더 작은 반환값이 나올수있따
int
write (int fd, const void *buffer, unsigned size) {
	check_address(buffer);
	check_address(buffer + size - 1);

	int write_bytes = 0;
	lock_acquire(&file_lock);

	//fd가 1이면 stdout 시스템 콜이기 떄문에 putbuf()을 이용해서 콘솔에다가 적어줘야한다
	if (fd == 1) {
		//should write all of buffer in one call
		//대신 버퍼 사이즈가 너무 크면 좀 나눠서 쓰도록 한다
		putbuf(buffer, size);
		//이 경우에는 콘솔에 우리가 버퍼를 다 쓸 수 있으니 결국 원래 size만큼 쓴다
		write_bytes = size;
	}

	//파일 용량을 끝났으면 원래는 파일을 더 늘려서 마저 쓰겠지만 여기서는 그냥 파일의 마지막주소까지 쓰고 여기까지 썼을때의 총 byte개수를 반환시킨다
	struct fd_structure *fd_elem = find_by_fd_index(fd);
	if (fd_elem == NULL) {
		write_bytes = -1;
	} else {
		struct file *curr_file = fd_elem->current_file;
		write_bytes = (int) file_write(curr_file, buffer, size);	//off_t 타입으로 나와니까 int으로 만들어주고 반환
	}

	lock_release(&file_lock);

	return write_bytes;
}

//fd의 파일이 다음으로 읽거나 쓸 next byte을 position으로 바꿔주는 void 함수
void
seek (int fd, unsigned position) {
	lock_acquire(&file_lock);
	struct fd_structure *fd_elem = find_by_fd_index(fd);
	if (fd_elem == NULL) {
		lock_release(&file_lock);
		return;
	} else {
		struct file *curr_file = fd_elem->current_file;
		file_seek(curr_file, position);
	}

	lock_release(&file_lock);
}

//주어진 fd에 있는 파일에서 읽거나 쓸 다음 byte의 주소를 반환한다 
//returns position of the next byte to be read/wrtie
//파일의 beginning을 기준으로 byte로 알려준다
unsigned
tell (int fd) {	
	unsigned result = -1;
	lock_acquire(&file_lock);

	struct fd_structure *fd_elem = find_by_fd_index(fd);
	//fd의 파일이 존재하지 않는경우 0을 반환한ㄷ
	if (fd_elem == NULL) {
		result = -1;
	} else {
		struct file *curr_file = fd_elem -> current_file;
		result = file_tell(curr_file);
	}	

	lock_release(&file_lock);

	return result;
}

//close file descriptor fd
//void file_close(struct file *file)을 사용하려면 fd에 맞는 file을 찾아내고 그걸 넘겨줘야한다
void
close (int fd) {
	lock_acquire(&file_lock);

	struct fd_structure *fd_elem = find_by_fd_index(fd);
	if (fd_elem == NULL) {
		lock_release(&file_lock);
		return;
	} else {
		struct file *curr_file = fd_elem->current_file;
		file_close(curr_file);
		//파일을 열떄 file descriptor table에서 이 파일을 위한 자리/메모리를 할당해주었고
		//우리는 각 파일 원소들을 thread안에 리스트로 관리하고 있기 때문에 이 리스트에서 없애주고 할당된 공간도 없애줘야한다
		struct list_elem curr_elem = fd_elem->elem;
		list_remove(&curr_elem);
		free(fd_elem);
	}

	lock_release(&file_lock);
}

struct fd_structure*
find_by_fd_index(int fd) {
	lock_acquire(&file_lock);

	struct thread *current_thread = thread_current();
	struct list *curr_fdt = &current_thread -> file_descriptor_table;
	struct list_elem *curr_elem = list_begin(curr_fdt);
	
	while (curr_elem != list_end(curr_fdt)) {
		struct fd_structure *curr_fd_elem = list_entry(curr_elem, struct fd_structure, elem);
		if (curr_fd_elem -> fd_index == fd) {
			//그럼 이 fd원소가 찾아진거기때문에 이 fd_structure element를 반환한다
			return curr_fd_elem;
		}

		curr_elem = list_next(curr_fdt);
	}

	//다 찾아봤는데 없으면 NULL을 반환하도록
	return NULL;
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

// git merge 위한