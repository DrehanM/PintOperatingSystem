#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/interrupt.h"
#include "list.h"

typedef struct thread_fd {
    int fd;
    struct file *f;
    struct list_elem elem;
} thread_fd_t;

void syscall_init (void);
void close_thread_fd(thread_fd_t *fd);

#endif /* userprog/syscall.h */