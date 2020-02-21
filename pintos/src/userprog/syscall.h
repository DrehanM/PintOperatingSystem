#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/interrupt.h"


void syscall_init (void);
void file_operation_handler(struct intr_frame *f);

#endif /* userprog/syscall.h */
