#include <stdbool.h>
#include <list.h>
#include <stdint.h>

#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

//struct lock *file_lock;

struct fd_structure {
	int fd_index;
	struct file *current_file;
    struct list_elem elem;
};

void syscall_init (void);

// 여기서도 함수 선언해줘야 함
//void syscall_handler (struct intr_frame * UNUSED);
// void exit(int status);
// tid_t fork (const char *thread_name, struct intr_frame *f);
// int exec (const char *file);
// int wait(tid_t pid);

#endif /* userprog/syscall.h */
