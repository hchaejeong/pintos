#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

#ifdef VM
//파일 내용을 lazy_load_segment으로 넘겨줄때 필수적으로 넘겨줘야하는 정보들
struct segment_info {
	struct file *page_file;
	off_t offset;
	size_t read_bytes;
	size_t zero_bytes;
};
#endif

#endif /* userprog/process.h */
