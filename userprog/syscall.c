#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <stdlib.h>
#include <string.h>
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
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/init.h"
#include "devices/input.h"
#include "vm/vm.h"
// project 4
#include "filesys/directory.h"
#include "filesys/inode.h"
#include "filesys/fat.h"

struct lock file_lock;

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

struct page* check_address(void *address);

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
void *mmap(void *addr, size_t length, int writable, int fd, off_t offset);
void munmap(void *addr);
bool chdir (const char *dir);
bool mkdir (const char *dir);
bool readdir (int fd, char *name);
bool isdir (int fd);
int inumber (int fd);
int symlink (const char *target, const char *linkpath);

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
	//NOT_REACHED();
	#ifdef VM
		struct thread *current = thread_current();
		current->user_stack_rsp = f->rsp;
	#endif

	switch (f->R.rax) {
		// %rax는 system call number이라고 적혀있다
		// include/lib/user/syscall.h에는 구현해야할 모든 경우?가 다 적혀있다.
		// 레지스터의 순서는 %rdi, %rsi, %rdx, %r10, %r8, %r9임!
		// uint64_t arg1 = f->R.rdi;
		// uint64_t arg2 = f->R.rsi;
		// uint64_t arg3 = f->R.rdx;

		case (SYS_HALT):
			halt();
			break;
		case (SYS_EXIT):
			//printf("여기는 들어오나?\n");
			//printf("%s: exit(%d)\n", thread_current()->name, f->R.rdi);
			//이걸 또 여기서 하면 에러가 뜬다. create할 때 exit이 출력이 안된달까...
			exit(f->R.rdi);
			break;
		case (SYS_FORK):
			//printf("sys_fork에는 들어오냐?/n");
			f->R.rax = fork((char *) f->R.rdi, f);
			break;
		case (SYS_EXEC):
			//printf("여기는 들어오나?\n");
			//f->R.rax = exec((char *) arg1);
			f->R.rax = exec((char *) f->R.rdi);
			break;
		case (SYS_WAIT):
			//printf("sys_wait에는 들어오냐?/n");
			f->R.rax = wait((tid_t) f->R.rdi);
			break;
		case (SYS_CREATE):
			f->R.rax = create((char *) f->R.rdi, (unsigned) f->R.rsi);
			break;
		case (SYS_REMOVE):
			f->R.rax = remove((char *) f->R.rdi);
			break;
		case (SYS_OPEN):
			f->R.rax = open((char *) f->R.rdi);
			break;
		case (SYS_FILESIZE):
			f->R.rax = filesize((int) f->R.rdi);
			break;
		case (SYS_READ):
			f->R.rax = read((int) f->R.rdi, (void *) f->R.rsi, (unsigned) f->R.rdx);
			break;
		case (SYS_WRITE):
			f->R.rax = write((int) f->R.rdi, (void *) f->R.rsi, (unsigned) f->R.rdx);
			break;
		case (SYS_SEEK):
			seek((int) f->R.rdi, (unsigned) f->R.rsi);
			break;
		case (SYS_TELL):
			f -> R.rax = tell((int)f->R.rdi);
			break;
		case (SYS_CLOSE):
			close((int) f->R.rdi);
			break;
		case (SYS_MMAP):
			f->R.rax = mmap((void *) f->R.rdi, (size_t) f->R.rsi, (int) f->R.rdx, (int) f->R.r10, (off_t) f->R.r8);
			//printf("f->R.rax: 0x%x\n", f->R.rax); // 다 잘 되는데...
			break;
		case (SYS_MUNMAP):
			munmap((void *) f->R.rdi);
			break;
		// project 4 부분의 system call!
		case (SYS_CHDIR):
			f->R.rax = chdir((const char *) f->R.rdi);
			break;
		case (SYS_MKDIR):
			f->R.rax = mkdir((const char *) f->R.rdi);
			break;
		case (SYS_READDIR):
			f->R.rax = readdir((int) f->R.rdi, (char *) f->R.rsi);
			break;
		case (SYS_ISDIR):
			f->R.rax = isdir((int) f->R.rdi);
			break;
		case (SYS_INUMBER):
			f->R.rax = inumber((int) f->R.rdi);
			break;
		case (SYS_SYMLINK):
			f->R.rax = symlink((const char *) f->R.rdi, (const char *) f->R.rsi);
		default:
			printf ("system call!\n");
			thread_exit ();
	}
	// printf ("system call!\n");
	// thread_exit ();
}

// lock_acquire할때 현재 돌아가고 있는 쓰레드가 락을 이미 가지고 있는데 요청한거면
// 필요없으니 
struct page* 
check_address(void *address) {
	// if (address >= LOADER_PHYS_BASE) {
	// 	exit(-1);
	// }

	// ASSERT(0);
	// ASSERT(1 < 0);
	if (lock_held_by_current_thread(&file_lock)) {
		lock_release(&file_lock);
		exit(-1);
		NOT_REACHED();
	}

	if (address == NULL) {
		exit(-1);
	}

	if (!is_user_vaddr(address)) {
		exit(-1);
	}

	//bad ptr인 경우 user virtual address에서 없는 경우일때 무조건 바로 exit하도록해야함.
	//struct thread *current = thread_current();
	struct page *pg = spt_find_page(&thread_current()->spt, address);
	if (pg == NULL) {
		exit(-1);
	} 

	return pg;
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
	/*
	if (!is_user_vaddr(file)) {
		exit(-1);
	}
	*/

	//file이 가르키고 있는 부분에 null sentinel이 있는경우 bad ptr이니까 바로 exit시켜야한다
	if (file[strlen(file) - 1] == '/') {
		return false;
	}

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
	if (file == NULL) {
		return -1;
	}
	check_address(file);
	/*
	if (!is_user_vaddr(file)) {
		exit(-1);
	}
	*/
	//printf("혹시 어디서 -1 에러가 나는걸까?\n");
	
	//fd0, fd1은 stdin, stdout을 위해 따로 빼둬야한다 -- 0이나 1을 리턴하는 경우는 없어야한다
	lock_acquire(&file_lock);
	struct file *actual_file = filesys_open(file);
	lock_release(&file_lock);
	
	if (!actual_file) {		//파일을 열지 못한 경우
		//free (fd_elem);		//할당한 공간을 사용하지 않았기 때문에 다시 free해줘서 다른 애들이 쓸 수 있게 해준다.
		//lock_release(&file_lock);
		return -1;
	}

	struct thread *current_thread = thread_current();

	struct list *curr_fdt = &current_thread -> file_descriptor_table;
	// bool fdt_empty = false;
	struct fd_structure *fd_elem = calloc(1, sizeof(struct fd_structure));
	int fd;
	if (fd_elem != NULL) {
		// lock_acquire(&file_lock);

		// lock_release(&file_lock);
		if (list_empty(curr_fdt)) {
			fd = 2;
			// lock_acquire(&file_lock);
			// //이 부분에서 파일을 넣는게 필요하다
			// fd_elem->fd_index = 3;
			// fd_elem -> current_file = actual_file;
			// // fdt_empty = true;
			// //fd_init++;
			// lock_release(&file_lock);
			// list_push_back(curr_fdt, &(fd_elem->elem));
		} else {
			fd = list_entry(list_back(curr_fdt), struct fd_structure, elem)->fd_index;
		}

		lock_acquire(&file_lock);
		fd_elem->fd_index = fd + 1;
		fd_elem -> current_file = actual_file;
		lock_release(&file_lock);

		list_push_back(curr_fdt, &(fd_elem->elem));
	} else {
		//lock_release(&file_lock);
		return -1;		//새로운 파일 열 공간이 부족한 경우 open되지 않기 때문에 -1을 리턴한다.
	}
	
	//파일이 제대로 잘 열렸으면 지금 fd 원소의 파일에 저장해놓는다
	//fd_elem->current_file = actual_file;
	//각 프로세스마다 자신만의 file descriptors을 가지고 있다 - child processes
	//현재 fdt 리스트의 맨 뒤에 넣기 때문에 현재 최대 index보다 1을 increment해줘야한다.
	// if (!fdt_empty) {
	// 	int prev_index = list_entry(list_back(curr_fdt), struct fd_structure, elem)->fd_index;
	// 	fd_elem->fd_index = prev_index + 1;
	// }
	
	//파일을 새로 열때마다 리스트에다가 추가해줘야하니까 맨 끝에 푸시를 해준다
	// if (!list_empty(curr_fdt)) {
	// 	struct list_elem curr_elem = fd_elem->elem;
	// 	list_push_back(curr_fdt, &curr_elem);
	// }
	
	//lock release한 다음에 결과를 반환해줘야한다
	return fd + 1;
}

//off_t file_length(struct file *file)함수를 사용한다
//fd에 있는 파일의 사이즈를 반환
int
filesize (int fd) {
	int size = 0;
  
	//저 file_length함수를 쓰기 위해서는 struct file *형태인 주어진 fd에 있는 파일을 가져와서 이거에 저 함수를 돌려야된다
	//따라서 우리 파일 테이블 리스트에서 fd 위치에 있는 것을 뽑아와야한다 -- 이걸 해주는 새로운 함수를 만들자
	struct fd_structure *fd_elem = find_by_fd_index(fd);
	if (fd_elem == NULL) {
		//찾지 못한거기때문에 -1을 반환시킨다
		size = -1;
	} else {
		lock_acquire(&file_lock);
		size = file_length(fd_elem->current_file);
		lock_release(&file_lock);
	}

	return size;
}

//fd의 파일에서 size bytes을 읽고 buffer에 넣는다
//실제로 읽은 byte 사이즈를 반환하고 파일을 읽지 못한 경우에는 -1을 반환시킨다
int
read (int fd, void *buffer, unsigned size) {
	struct page *start = check_address(buffer);
	struct page *end = check_address(buffer + size - 1);

	if (start->write == false || end->write == false) {
		exit(-1);
	}

	int read_bytes = 0;

	//fd가 0이면 파일에서 읽지 않고 keyboard에서 input_getc()로 input을 읽어야한다
	if (fd == 0) {
		lock_acquire(&file_lock);
		uint8_t key = input_getc();	 //user가 입력하도록 기다리고 입력하는 키보드 key를 반환한다
		lock_release(&file_lock);
	}
	struct fd_structure *fd_elem = find_by_fd_index(fd);
	if (fd_elem == NULL) {
		//exit(-1);
		read_bytes = -1;
	} else {
		//지금 fd에 맞는 파일을 뽑아오고 이 파일을 읽어줘야한다
		struct file *curr_file = fd_elem->current_file;
		if (curr_file == NULL) {
			return -1;
		}
		lock_acquire(&file_lock);
		//struct file *curr_file = fd_elem->current_file;
		read_bytes = file_read(curr_file, buffer, size);
		lock_release(&file_lock);
	}
	return read_bytes;
}

//buffer에서 size bytes를 fd의 파일에다가 쓰는 함수
//실제로 써지는 byte만큼을 반환한다 - 안 써지는 byte들도 있을수 있기 때문에 size 보다 더 작은 반환값이 나올수있따
int
write (int fd, const void *buffer, unsigned size) {
	struct page *start = check_address(buffer);
	//struct page *end = check_address(buffer + size - 1);
	//printf("doing write syscall");

	int write_bytes = 0;
	// if (size == 0) {
	// 	return 0;
	// }
	//printf("설마 여기가 연관?\n"); // 연관되어있음
	
	//fd가 1이면 stdout 시스템 콜이기 떄문에 putbuf()을 이용해서 콘솔에다가 적어줘야한다
	if (fd == 0) {
		return -1;
	} else if (fd == 1) {
		//should write all of buffer in one call
		//대신 버퍼 사이즈가 너무 크면 좀 나눠서 쓰도록 한다
		//NOT_REACHED();
		//printf("fd == 1일때야?\n");
		lock_acquire(&file_lock);
		putbuf(buffer, size);
		lock_release(&file_lock);
		//이 경우에는 콘솔에 우리가 버퍼를 다 쓸 수 있으니 결국 원래 size만큼 쓴다
		write_bytes = size;
	} else {
		//printf("fd가 다른 값일때야\n");
		//파일 용량을 끝났으면 원래는 파일을 더 늘려서 마저 쓰겠지만 여기서는 그냥 파일의 마지막주소까지 쓰고 여기까지 썼을때의 총 byte개수를 반환시킨다
		struct fd_structure *fd_elem = find_by_fd_index(fd);
		if (fd_elem == NULL) {
			write_bytes = -1;
		} else {
			lock_acquire(&file_lock);
			struct file *curr_file = fd_elem->current_file;
			write_bytes = (int) file_write(curr_file, buffer, size);	//off_t 타입으로 나와니까 int으로 만들어주고 반환
			lock_release(&file_lock);
		}
	}

	return write_bytes;
}

//fd의 파일이 다음으로 읽거나 쓸 next byte을 position으로 바꿔주는 void 함수
void
seek (int fd, unsigned position) {
	struct fd_structure *fd_elem = find_by_fd_index(fd);
	if (fd_elem == NULL) {
		return;
	} else {
		lock_acquire(&file_lock);
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

	struct fd_structure *fd_elem = find_by_fd_index(fd);
	//fd의 파일이 존재하지 않는경우 0을 반환한ㄷ
	if (fd_elem == NULL) {
		result = -1;
	} else {
		lock_acquire(&file_lock);
		struct file *curr_file = fd_elem -> current_file;
		result = file_tell(curr_file);
		lock_release(&file_lock);
	}	

	return result;
}

//close file descriptor fd
//void file_close(struct file *file)을 사용하려면 fd에 맞는 file을 찾아내고 그걸 넘겨줘야한다
void
close (int fd) {
	struct fd_structure *fd_elem = find_by_fd_index(fd);
	if (fd_elem == NULL) {
		return;
	} else {
		lock_acquire(&file_lock);
		struct file *curr_file = fd_elem->current_file;
		file_close(curr_file);
		lock_release(&file_lock);
		//파일을 열떄 file descriptor table에서 이 파일을 위한 자리/메모리를 할당해주었고
		//우리는 각 파일 원소들을 thread안에 리스트로 관리하고 있기 때문에 이 리스트에서 없애주고 할당된 공간도 없애줘야한다
		struct list_elem curr_elem = fd_elem->elem;
		list_remove(&curr_elem);
		//palloc_free_page(fd_elem);
		free(fd_elem);
	}
}

//주어진 fd를 가지고 있는 파일을 찾을때 필요하므로 fd_structure을 전체 반환해서 list_elem도 쓸 수 있게
struct fd_structure*
find_by_fd_index(int fd) {
	struct thread *current_thread = thread_current();
	struct list *curr_fdt = &current_thread -> file_descriptor_table;
	struct list_elem *curr_elem = list_begin(curr_fdt);
	
	while (curr_elem != list_end(curr_fdt)) {
		struct fd_structure *curr_fd_elem = list_entry(curr_elem, struct fd_structure, elem);
		if (curr_fd_elem -> fd_index == fd) {
			//그럼 이 fd원소가 찾아진거기때문에 이 fd_structure element를 반환한다
			return curr_fd_elem;
		}

		curr_elem = list_next(curr_elem);
	}

	//다 찾아봤는데 없으면 NULL을 반환하도록
	return NULL;
}

void
exit (int status) {
	//printf("status: %d\n", status);
	thread_current()->exit_num = status;
	printf("%s: exit(%d)\n", thread_current()->name, thread_current()->exit_num);
	thread_exit();
}

tid_t
fork (const char *thread_name, struct intr_frame *f) {
	//printf("여긴 들어가?\n");
	return process_fork(thread_name, f);
}

bool
addr_check(void *address) {
	// 지금은 user의 addr에 배치되어야 있어야 하고, page가 할당된 상태어야 함
	// return is_user_vaddr(address);
	// 괜찮으면 true return, 안괜찮으면 false return
	return is_user_vaddr(address) && (pml4_get_page(thread_current()->pml4, address) != NULL);
	//return !is_kernel_vaddr(address) && (pml4_get_page(thread_current()->pml4, address) != NULL);
	//return (is_kernel_vaddr(address) || pml4_get_page(thread_current()->pml4, address) == NULL);
}

int
exec (const char *file) {
	// gitbook에 적힌 내용 요약
	// 거기서는 cmd_line이지만, 결국에는 file name으로 들어갈 것임
	// 성공하면 아무것도 return하지 않고, 오류나면 exit -1로 종료함
	// exec라는 이름의 thread의 이름을 바꾸지 않음
	// fd는 exec call 상황에서 open 상태로 남아있음

	// 일단 먼저 뭐든 간에 올바른 address인지 check하는 과정이 필요함.
	//printf("addr check: %d\n", !addr_check(file));
	if (!addr_check(file)) {
		//printf("addr check: %d\n", addr_check(file));
		//printf("이거 때문인가\n"); // 이거 때문이네...
		exit(-1);
	}
	/*
	if (!is_user_vaddr(file)) {
		exit(-1);
	}
	*/
	// process_create_initd에서 strlcpy 썼던 것처럼 이름을 복사해서, 그 이름으로 exec 시킨다!
	// 일단 하나 page를 할당받고, 이상하면 exit.

	// char *file_name = palloc_get_page(0);
	// palloc_get_page(0)으로 하면 안됨. PAL_ZERO와는 완전히 다름.
	// 0으로 완전히 채워진!! 즉, 우리가 바로 수정하고 사용할 수 있는 page가 되려면
	// 0이 아닌 PAL_ZERO를 사용해야함.
	char *file_name = palloc_get_page(PAL_ZERO);
	int file_size = strlen(file) + 1;

	int exec_result;
	if (file_name == NULL) {
		exit(-1);
	} else {
		strlcpy(file_name, file, file_size);
		exec_result = process_exec(file_name);
		//palloc_free_page(file_name);
		if (exec_result < 0) {
		//if (process_exec(file_name) < 0) {
			exit(-1);
			//return -1;
		}
	}
	thread_current()->exit_num = exec_result;
	//NOT_REACHED(); // 이 명령어를 넣어야 이 명령어의 도달 여부를 알 수 있음
	//return 0;
	return exec_result;
}

int
wait(tid_t pid) {
	return process_wait(pid);
}

void *mmap(void *addr, size_t length, int writable, int fd, off_t offset) {
	/* 그냥 mmap 함수에 대한 설명:
	length bytes를 map하는데, addr 위치의 process의 va space에 함.
	전체 file은 addr부터 시작하는 연속적인 virtual page에 map됨.
	length가 PGSIZE의 배수가 아니면, 몇 byte는 EOF 넘어서 약간 "stick out"됨.
	page fault 할때는, 이렇게 튀어나온 byte들을 zero로 set하고,
	page가 disk로 written back했을 때 삭제한다.
	만약 성공하면, file이 map된 va를 return한다.
	실패하면, NULL을 반환한다. */
	/* 파일의 길이 == 0이면 fail.
	addr이 page-aligned가 아니거나, page의 range가 이미 존재하는 mapped pages set에 overlap하면 fail.
	(executable load time에 stack이나 page가 mapping되는 것 포함)
	Linux에서는, addr == NULL이면 mapping을 create할 수 있는 addr을 kernel이 찾음
	즉, 그냥 given addr로 mmap을 attempt하면 됨.
	이에 따라 addr == 0이면 무조건 fail할 것 - pintos code는 virtual page 0은 mapped 되어있지 않다고 판단하기 때문
	fd가 console I/O를 represent하는 것도 map이 불가함. (fail)
	length == 0이면 mmap은 fail. */
	/* anon page처럼 lazy: page object 만들 때 vm_alloc_page_with_initializer or vm_alloc_page 이용 */

	// 하나하나씩 천천히 해보자.

	/*
	printf("addr: 0x%x\n", addr);
	printf("length: %d\n", length);
	printf("fd: %d\n", fd);
	printf("offset: 0x%x\n", offset);
	*/
	/* mmap-kernel에서, length를 int로 출력하면 음수, -67104768가 나옴. 즉, 너무 커졌다는 것...
	그렇다면 0보다 작은 것 뿐만 아니라 특정 길이를 넘어서 커널에 가는것도 체크해야함 */
	
	if (length <= 0) {
		/* length == 0 */
		return NULL;
	} else if (length >= KERN_BASE) {
		/* 윗 줄에 이 줄을 추가한 이유를 적어둠 ~~ */
		return NULL;
	} if (fd == 0 || fd == 1) {
		/* I/O인 경우는 fail */
		return NULL;
	} else if (addr == NULL) {
		/* addr == NULL이면 무조건 fail */
		return NULL;
	} else if (pg_round_down(addr) != addr) {
		/* addr이 page aligned가 아닌 경우 fail */
		return NULL;
	} else if (is_kernel_vaddr(addr)) {
		/* 당연히 kernel이면 fail */
		//printf('addr is in kernel');
		return NULL;
	} else if (spt_find_page(&thread_current()->spt, addr)) {
		/* overlap하는 경우는 fail */
		return NULL;
	} else if (pg_round_down(offset) != offset) {
		/* 이 또한 offset이 이상한 위치인 경우임. fail */
		return NULL;
	} else {
		/* 일단은 이제 파일을 열 수 있는 조건이 되었음 */
		//printf("여기서 에러가 뜸?\n");
		struct fd_structure* fd_elem = find_by_fd_index(fd);
		//printf("여기서 에러가 뜸?\n");
		//ASSERT(fd_elem != NULL); // file만 판단하는게 아니라 fd_elem도 판단해야 하는거였어..
		// mmap-bad-fd 오류 해결!!
		if (fd_elem == NULL) {
			/* file을 열어보기 전에 fd_elem부터 확인해줘야 함을 잊지말자!!! */
			return NULL;
		} else {
			struct file *file = fd_elem->current_file;
			if (file == NULL) {
				/* file이 NULL이면 fail */
				return NULL;
			} else if (file_length(file) == 0) {
				/* 파일의 길이 == 0이면 fail */
				return NULL;
			} else if (file_length(file) <= offset) {
				/* offset byte부터 시작하는 file이 offset보다 작으면 당연히 fail이겠지 */
				return NULL;
			} else {
				/* 이제 최종적으로 do_mmap 할 수 있음! */
				//printf("syscall.c의 do_mmap에는 들어가지?\n"); //okay
				return do_mmap(addr, length, writable, file, offset);
			}
		}
	}
}

void munmap(void *addr) {
	/* munmap 함수 설명:
	addr 주변 특정 range부분의 mapping된 걸 unmmap하는 역할.
	page들이 다시 file에 쓰여지고, 안 쓰여진 페이지는 쓰이면 안 됨.
	unmap되므로, virtual page list에서 page가 제거되어야 함 */
	/* 모든 mapping은 exit이 발동되면 무조건 unmap됨.
	unmap될 때는, process가 썼던 모든 page들이 file로 written back되고, 안 쓰여진 애들은 written되면 안된다.
	그리고 page들은 process의 list of virtual pages에서 사라진다
	file을 close or remove하는건 unmap이랑은 아무 관련이 없음.
	각 mapping마다 seperate & independent한 ref를 쓰려면 file_reopen 함수를 쓰는 것임.
	2개 이상의 process가 같은 file에 map되어있을 때,
	각 process가 consistent datafmf 볼 수 있는 requirement는 없음. */

	// 그냥 여기서는 아무 상관없이, do_munmap으로만 가면 되는듯?
	do_munmap(addr);
}



/* project 4 */

bool chdir (const char *dir) {
	/* process의 현재 작업 directory를 dir로 변경하면 됨.
	dir은 상대적, 절대적 모두 가능
	성공하면 true, 실패하면 false 반환 */

	check_address(dir);

	bool success = true;

	// 먼저 dir이 NULL이면 안되겠지 ㅇㅇ
	if (dir == NULL) {
		success = false;
	}

	struct dir *real_dir = dir_open_root(); // 일단 기본이 되는 root 경로로 열어두고 시작
	if (dir[0] != '/') {
		// 그런데 dir이 상대 경로면 현재 thread의 dir로 세팅해둬야 함
		// '/'로 시작하면 절대 경로, 아니면 상대 경로
		// 그냥 open ㄴㄴ. reopen함으로써 서로 간섭 안나게 해야함
		real_dir = dir_reopen(thread_current()->current_dir);
	}

	// dir도 복제본을 만들어둬야 함. 안 그러면 저 dir도 망가진다 볼 수 있음
	char *copy_dir = (char *)malloc((strlen(dir)+1) * sizeof(char));
	strlcpy(copy_dir, dir, strlen(dir) + 1);

	// 채정이가 process.c의 process_exec에서 한 것처럼 나누면 됨
	char * save;
	char * dir_token = strtok_r(copy_dir, "/", &save);
	struct inode *inode = NULL;
	while (dir_token != NULL) {
		// 주어진 dir 안에 token 이름의 파일이 있는지 탐색, 있으면 inode 정보 저장
		bool success_lookup = dir_lookup(real_dir, dir_token, &inode);
		// 이렇게 생긴 inode가 directory인지 판단
		bool is_inode_dir = inode_is_directory(inode); // 채정이가 만들어 둔 inode_is_directory 함수를 쓰면 된다
		if (!(success_lookup && is_inode_dir)) {
			// lookup한 결과가 없거나, inode가 directory가 아니면 (file이면) return false임
			dir_close(real_dir); // 일단 open한 dir은 닫아야지
			success = false;
			break; // while문에서 바로 나와서 return false하면 됨
		} else {
			dir_close(real_dir); // 기존 dir 정보는 닫고
			real_dir = dir_open(inode); // inode에 이제 dir이 들어가 있을테니까, 그 dir 정보를 열기
			// 이 과정을 dir 끝까지 돌리는거지!
			dir_token = strtok_r(NULL, "/", &save);
		}
	}

	free(copy_dir);

	if (success) {
		// 위 과정을 돌면서 아무 문제가 없었으면
		// 이제 실제 thread의 dir을 바꿔주는 작업을 하면 됨
		// real_dir에 저장해줘야 할 dir이 저장이 되어있음
		dir_close(thread_current()->current_dir);
		thread_current()->current_dir = real_dir;
		return true;
	} else {
		// 위 과정을 돌면서 success가 false가 되면 return false
		return false;
	}
}

bool mkdir (const char *dir) {
	/* 상대적, 절대적 모두 가능한 dir이라는 directory를 만듦.
	성공하면 true, 실패하면 false 반환.
	dir이 이미 존재하거나 & 그 앞의 경로가 없는 경우에는 false
	mkdir("/a/b/c")는 /a/b가 이미 exist하고 /a/b/c가 없는 경우에만 성공한다는 것 */

	if (dir == NULL || strlen(dir) == 0) {
		return false;
	}
	// 여기는 chdir과 같음. 경로 초기 세팅
	struct dir *real_dir = dir_open_root(); // 일단 기본이 되는 root 경로로 열어두고 시작
	if (dir[0] != '/') {
		// 그런데 dir이 상대 경로면 현재 thread의 dir로 세팅해둬야 함
		// '/'로 시작하면 절대 경로, 아니면 상대 경로
		// 그냥 open ㄴㄴ. reopen함으로써 서로 간섭 안나게 해야함
		real_dir = dir_reopen(thread_current()->current_dir);
	}

	// chdir이랑 일단은 같은 방법으로 parse해서 돌리기
	char *copy_dir = (char *)malloc((strlen(dir)+1) * sizeof(char));
	strlcpy(copy_dir, dir, strlen(dir) + 1);

	char *save;
	char *dir_token = strtok_r(copy_dir, "/", &save);
	struct inode *inode = NULL;

	// 경로의 마지막은 file name임. 이걸 빼내와야 함. 일단 malloc으로 공간 만들어주기
	char *file_name = (char *)malloc((strlen(dir)+1) * sizeof(char));
	if (dir_token == NULL) {
		strlcpy(file_name, ".", 2); // "/"인 경우에는, file_name은 .이 되어야 함
	}
	
	// 여기서의 목적은 file_name만 parsing 해내는 것!
	while (dir_token != NULL) {
		bool success_lookup = dir_lookup(real_dir, dir_token, &inode);
		bool is_inode_dir = inode_is_directory(inode);
		if (!(success_lookup && is_inode_dir)) {
			dir_close(real_dir);
			NOT_REACHED();
			//inode_close(inode);
			real_dir = NULL; // dir이 없는 거니까 dir은 null로 세팅해줘야
			break;
		} else {
			// 링크 파일인 경우는 앞서서 한 번 더 돌리면 될 것 같음
			if (check_symlink(inode)) {
				// 아니 대체 왜 inode->data.symlink가 안되는건데 ㅋㅋ
				// inode의 path를 복사해온다!
				char *inode_path = (char*)malloc(sizeof(length_symlink_path(inode)));
				strlcpy(inode_path, inode_data_symlink_path(inode), length_symlink_path(inode));
				
				// inode path 경로에 symlink 뒷부분을 갖다붙이면 됨
				// ../file 이런 식으로 되어있던 걸 inode path/file 이런 형식으로!
				strlcat(inode_path, "/", strlen(inode_path) + 2);
				strlcat(inode_path, save, strlen(inode_path) + strlen(save) + 1);

				dir_close(real_dir);

				// 그리고 while문 다시 시작해야함!
				real_dir = dir_open_root();
				if (dir[0] != '/') {
					real_dir = dir_reopen(thread_current()->current_dir);
				}
				strlcpy(copy_dir, inode_path, strlen(inode_path) + 1);
				free(inode_path);
				dir_token = strtok_r(copy_dir, "/", &save);
				continue;
			}


			dir_close(real_dir);
			real_dir = dir_open(inode);
			// 근데 여기서, 경로의 마지막은 file의 이름이므로 그걸 저장해야함
			char *check_next = strtok_r(NULL, "/", &save);
			if (check_next == NULL) {
				// file_name에 file name 복사해두기!
				strlcpy(file_name, dir_token, strlen(dir_token) + 1);
				break;
			} else {
				dir_token = file_name;
			}
		}
	}

	// 새로운 chain을 만들어서 inode sector 번호를 받아야 함
	cluster_t inode_sector_num = fat_create_chain(0);
	// dir이 NULL이면 당연히 error
	if (real_dir == NULL) { goto error; }
	
	// 이렇게 만들어진 sector에 위에서 받은 file_name의 dir을 만들어야 함. 안 만들어지면 당연히 error
	bool create_dir = dir_create(inode_sector_num, 16);
	if (!create_dir) { goto error; }

	// dir에 file_name entry를 추가해줘야 함. 잘 안되면 당연히 goto error
	bool add_file_name = dir_add(real_dir, file_name, inode_sector_num);
	if (!add_file_name) { goto error; }

	dir_close(real_dir);
	free(copy_dir);
	free(file_name);

	return true;
error:
	if (inode_sector_num != 0) {
		fat_remove_chain(inode_sector_num, 0);
	}
	dir_close(real_dir);
	free(copy_dir);
	free(file_name);
	return false;
}

bool readdir (int fd, char *name) {

	check_address(name);

	bool check = true;
	struct file *file = NULL;

	if (fd == NULL || name == NULL) {
		check = false;
	} else {
		struct fd_structure* fd_elem = find_by_fd_index(fd);
		if (fd_elem == NULL) {
			check = false;
		} else {
			file = fd_elem->current_file;
			if (file == NULL) {
				check = false;
			} else {
				// inode가 dir이면 true, 아니면 false
				check = inode_is_directory(file_get_inode(file));
			}
		}
	}
	
	if (check) {
		struct dir *file_pointer = file; // file을 가리키는 pointer을 dir 형태로 저장
		if (dir_pos(file_pointer) == 0) {
			dir_change_pos(file_pointer);
		}
		return dir_readdir(file_pointer, name);
	} else {
		return false;
	}
}

bool isdir (int fd) {
	/* fd가 directory를 나타내면 true, 그냥 file을 나타내면 false */
	struct fd_structure* fd_elem = find_by_fd_index(fd);
	if (fd_elem == NULL) {
		return false;
	} else {
		struct file *file = fd_elem->current_file;
		if (file == NULL) {
			return false;
		} else {
			return inode_is_directory(file_get_inode(file));
		}
	}
}

int inumber (int fd) {
	/* file 또는 dir을 표현하는 fd의 inode number을 반환
	inode number은 file이나 directory를 지속적으로 식별함
	file이 존재하는 동안은 고유함.
	pintos에서는 inode의 sector num이 inode num으로 사용되면 됨*/
	struct fd_structure* fd_elem = find_by_fd_index(fd);
		if (fd_elem == NULL) {
		return 0;
	} else {
		struct file *file = fd_elem->current_file;
		if (file == NULL) {
			return 0;
		} else {
			return inode_get_inumber(file_get_inode(file));
		}
	}
}

int symlink (const char *target, const char *linkpath) {
	/* soft link임.
	다른 file 또는 directory를 참조하는 pseudo file 개체임.
	/
	├── a
	│   ├── link1 -> /file
	│   │
	│   └── link2 -> ../file
	└── file
	여기서 link1은 절대 경로이고 link2는 상대 경로이며,
	link1과 link2 모두 /file을 읽는 것과 같음 */

	
	// mkdir에서 썼던 parsing 코드를 들고와야함

	if (linkpath == NULL || strlen(linkpath) == 0) {
		return false;
	}
	
	struct dir *real_dir = dir_open_root();
	if (linkpath[0] != '/') {
		real_dir = dir_reopen(thread_current()->current_dir);
	}

	char *copy_linkpath = (char *)malloc((strlen(linkpath)+1) * sizeof(char));
	strlcpy(copy_linkpath, linkpath, strlen(linkpath) + 1);

	char *save;
	char *linkpath_token = strtok_r(copy_linkpath, "/", &save);
	struct inode *inode = NULL;

	char *file_name = (char *)malloc((strlen(linkpath)+1) * sizeof(char));
	if (linkpath_token == NULL) {
		strlcpy(file_name, ".", 2);
	}

	// 링크 파일인 경우는 앞서서 한 번 더 돌리면 될 것 같음
	
	// 여기서의 목적은 file_name만 parsing 해내는 것!
	while (linkpath_token != NULL) {
		bool success_lookup = dir_lookup(real_dir, linkpath_token, &inode);
		bool is_inode_dir = inode_is_directory(inode);
		if (!(success_lookup && is_inode_dir)) {
			dir_close(real_dir);
			inode_close(inode);
			real_dir = NULL;
			break;
		} else {
			// 링크 파일인 경우는 앞서서 한 번 더 돌리면 될 것 같음
			if (check_symlink(inode)) {
				// 아니 대체 왜 inode->data.symlink가 안되는건데 ㅋㅋ
				// inode의 path를 복사해온다!
				char *inode_path = (char*)malloc(sizeof(length_symlink_path(inode)));
				strlcpy(inode_path, inode_data_symlink_path(inode), length_symlink_path(inode));
				
				// inode path 경로에 symlink 뒷부분을 갖다붙이면 됨
				// ../file 이런 식으로 되어있던 걸 inode path/file 이런 형식으로!
				strlcat(inode_path, "/", strlen(inode_path) + 2);
				strlcat(inode_path, save, strlen(inode_path) + strlen(save) + 1);

				dir_close(real_dir);
				
				// 그리고 while문 다시 시작해야함!
				real_dir = dir_open_root();
				if (linkpath[0] != '/') {
					real_dir = dir_reopen(thread_current()->current_dir);
				}
				strlcpy(copy_linkpath, inode_path, strlen(inode_path) + 1);
				free(inode_path);
				linkpath_token = strtok_r(copy_linkpath, "/", &save);
				continue;
			}

			dir_close(real_dir);
			real_dir = dir_open(inode);
			// 근데 여기서, 경로의 마지막은 file의 이름이므로 그걸 저장해야함
			char *check_next = strtok_r(NULL, "/", &save);
			if (check_next == NULL) {
				// file_name에 file name 복사해두기!
				strlcpy(file_name, linkpath_token, strlen(linkpath_token) + 1);
				break;
			} else {
				linkpath_token = file_name;
			}
		}
	}

	// 새로운 chain을 만들어서 inode sector 번호를 받아야 함
	cluster_t inode_sector_num = fat_create_chain(0);

	// dir이 NULL이면 당연히 error
	if (real_dir == NULL) { goto error; }
	
	// 이렇게 만들어진 sector에 위에서 받은 file_name의 dir을 만들어야 함. 안 만들어지면 당연히 error
	bool link_inode = create_link_inode(inode_sector_num, 16);
	if (!link_inode) { goto error; }

	// dir에 file_name entry를 추가해줘야 함. 잘 안되면 당연히 goto error
	bool add_file_name = dir_add(real_dir, file_name, inode_sector_num);
	if (!add_file_name) { goto error; }

	dir_close(real_dir);
	free(copy_linkpath);
	free(file_name);

	return 0; // 성공하면 0 반환
error:
	if (inode_sector_num != 0) {
		fat_remove_chain(inode_sector_num, 0);
	}
	dir_close(real_dir);
	free(copy_linkpath);
	free(file_name);

	return -1; // 실패하면 -1 반환
}