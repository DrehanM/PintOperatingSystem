#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);
static struct lock global_file_lock;
static size_t fd_count = 2;

typedef struct thread_fd {
  int fd;
  file *file;
  struct list_elem elem;
} thread_fd_t;


file *
get_file_from_fd(int fd) {
  struct list l = thread_current()->fd_map;
  thread_fd_t *w;
  for (struct list_elem *e = list_begin(&l); e->next != NULL; e = e->next) {
    w = list_entry(e, thread_fd_t, elem);
    if (w->fd == fd) {
      return w->file;
    }
  }
  return NULL;
}

void
syscall_init (void)
{
  lock_init(&global_file_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

bool create(const char * file, unsigned initial_size){
  return filesys_create(file, initial_size);
}

bool remove(const char * file) {
  return filesys_remove(file);
}

int open(const char * file) {
  struct file *f = filesys_open(file);
  if (f == NULL) {
    return -1;
  }
  if ( (thread_fd_t *fd = malloc(sizeof(thread_fd_t)) ) == NULL) {
    file_close(f);
    return -1;
  }
  fd->fd = fd_count;
  fd->file = f;
  struct list l = thread_current()->fd_map;
  list_push_back(&l, fd->elem);
  fd_count++;
  return fd_count-1;
}

int filesize(int fd) {
  file *f = get_file_from_fd(fd);
  return file_length(f);
}

static void
file_operation_handler(struct intr_frame *f) {
  uint32_t* args = ((uint32_t*) f->esp);
  lock_acquire(&global_file_lock);
  switch (args[0]) {
    // LUKE AND CHRIS

    case SYS_CREATE:
      f->eax = create(args[1], args[2]);
      break;                /* Create a file. */
    case SYS_REMOVE:
      f->eax = remove(args[1]);
      break;                 /* Delete a file. */
    case SYS_OPEN:
      f->eax = open(args[1]);
      break;                   /* Open a file. */
    case SYS_FILESIZE:
      f->eax = filesize(args[1]);
      break;               /* Obtain a file's size. */

    // BEN AND DIEGO
    case SYS_READ:
      break;                   /* Read from a file. */
    case SYS_WRITE:
      break;                  /* Write to a file. */
    case SYS_SEEK:
      break;                   /* Change position in a file. */
    case SYS_TELL:
      break;                   /* Report current position in a file. */
    case SYS_CLOSE:
      break;

  }

  lock_release(&global_file_lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  uint32_t* args = ((uint32_t*) f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  if (args[0] == SYS_EXIT) {
    f->eax = args[1];
    printf ("%s: exit(%d)\n", &thread_current ()->name, args[1]);
    thread_exit ();
  } else if (args[0] == SYS_HALT) {
    return;
  } else if (args[0] == SYS_EXEC) {
    return;
  } else if (args[0] == SYS_WAIT) {
    return;
  } else { // ITS FILESYSTEM CALL
    file_operation_handler(f);
  }
}
