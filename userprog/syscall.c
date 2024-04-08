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


struct lock file_lock;
//int fd_init;

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void check_address(void *address);

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
	//syscall말고 일반 파일이 들어갈 수 있는 fd 초기화
	fd_init = 2;
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// 여기서 이제, include/lib/syscall-nr.h를 보면 각 경우의 system call #을 알 수 있다.
	// 그리고, 여기서부터는 레지스터를 사용해야 한다. 이건 include/threads/interrupt.h에 있다!
	// intr_frame 안에는 register 모임?인 R이 있고, R 안에는 깃헙 io링크에 있는 %rax 이런애들이 다 있다!
	//NOT_REACHED();
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
	// if (address >= LOADER_PHYS_BASE) {
	// 	exit(-1);
	// }

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
	struct thread *current = thread_current();
	if (pml4_get_page(current->pml4, address) == NULL) {
		exit(-1);
	}
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

bool
compare_fd_func (const struct list_elem *a,
                  const struct list_elem *b,
                  void *aux) {
   struct fd_structure *a_fd_elem = list_entry(a, struct fd_structure, elem);
   struct fd_structure *b_fd_elem = list_entry(b, struct fd_structure, elem);

   return a_fd_elem->fd_index < b_fd_elem->fd_index;
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
		
		// fd_elem->current_file = actual_file;
		// fd_elem->fd_index = fd_init;
		// fd_init++;

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
	// fd_init++;
	

	//list_insert_ordered(curr_fdt, &fd_elem->elem, compare_fd_func, NULL);

	//파일이 제대로 잘 열렸으면 지금 fd 원소의 파일에 저장해놓는다
	//fd_elem->current_file = actual_file;
	//fd_init++;
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
	
  
	//lock_release(&file_lock);

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
	check_address(buffer);
	check_address(buffer + size - 1);

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
		lock_acquire(&file_lock);
		struct file *curr_file = fd_elem->current_file;
		read_bytes = (int) file_read(curr_file, buffer, size);
		lock_release(&file_lock);
	}

	return read_bytes;
}

//buffer에서 size bytes를 fd의 파일에다가 쓰는 함수
//실제로 써지는 byte만큼을 반환한다 - 안 써지는 byte들도 있을수 있기 때문에 size 보다 더 작은 반환값이 나올수있따
int
write (int fd, const void *buffer, unsigned size) {
	check_address(buffer);
	check_address(buffer + size - 1);
	//printf("doing write syscall");

	int write_bytes = 0;
	// if (size == 0) {
	// 	return 0;
	// }
	
	//fd가 1이면 stdout 시스템 콜이기 떄문에 putbuf()을 이용해서 콘솔에다가 적어줘야한다
	if (fd == 0) {
		return -1;
	} else if (fd == 1) {
		//should write all of buffer in one call
		//대신 버퍼 사이즈가 너무 크면 좀 나눠서 쓰도록 한다
		//NOT_REACHED();
		lock_acquire(&file_lock);
		putbuf(buffer, size);
		lock_release(&file_lock);
		//이 경우에는 콘솔에 우리가 버퍼를 다 쓸 수 있으니 결국 원래 size만큼 쓴다
		write_bytes = size;
	} else {
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

		curr_elem = list_next(curr_fdt);
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
	// process_create_initd에서 strlcpy 썼던 것처럼 이름을 복사해서, 그 이름으로 exec 시킨다!
	// 일단 하나 page를 할당받고, 이상하면 exit.

	// char *file_name = palloc_get_page(0);
	// palloc_get_page(0)으로 하면 안됨. PAL_ZERO와는 완전히 다름.
	// 0으로 완전히 채워진!! 즉, 우리가 바로 수정하고 사용할 수 있는 page가 되려면
	// 0이 아닌 PAL_ZERO를 사용해야함.
	char *file_name = palloc_get_page(PAL_ZERO);
	int file_size = strlen(file) + 1;
	if (file_name == NULL) {
		exit(-1);
	} else {
		strlcpy(file_name, file, file_size);
		if (process_exec(file_name) < 0) {
			exit(-1);
			//return -1;
		}
	}

	NOT_REACHED(); // 이 명령어를 넣어야 이 명령어의 도달 여부를 알 수 있음
	return 0;
}

int
wait(tid_t pid) {
	return process_wait(pid);
}