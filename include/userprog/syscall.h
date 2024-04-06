#include <stdbool.h>
#include <list.h>
#include <stdint.h>

#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

//struct lock *file_lock;
int fd_init;

//그냥 struct thread안에 이거를 넣으려고 했는데 각 프로세스당 할당되는게 하나의 file포인터가 아니고 fd_index랑 file포인터를 가지고 있는 원소들의 집합
//즉 여러개의 fd_structure을 가진 테이블을 각 thread가 가지고있어야하니까 list of fd_structure으로 생각해야한다
struct fd_structure {
	int fd_index;
	struct file *current_file;
    struct list_elem elem;
};

void syscall_init (void);

bool
compare_fd_func (const struct list_elem *a,
                  const struct list_elem *b,
                  void *aux);
// 여기서도 함수 선언해줘야 함
//void syscall_handler (struct intr_frame * UNUSED);

// void exit(int status);
// tid_t fork (const char *thread_name, struct intr_frame *f);
// int exec (const char *file);
// int wait(tid_t pid);

#endif /* userprog/syscall.h */
