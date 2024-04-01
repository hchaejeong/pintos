#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

// 여기서도 함수 선언해줘야 함
//void syscall_handler (struct intr_frame * UNUSED);
void halt(void);

#endif /* userprog/syscall.h */
