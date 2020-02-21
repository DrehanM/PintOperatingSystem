#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdio.h>

void syscall_init (void);
void file_operation_handler(struct intr_frame *f);
FILE *get_file_from_fd(int fd);
#endif /* userprog/syscall.h */
