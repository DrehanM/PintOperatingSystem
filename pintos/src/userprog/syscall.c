#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "../filesys/file.h"
#include "../filesys/filesys.h"
#include "threads/vaddr.h"
#include "process.h"
#include "threads/palloc.h"


static void syscall_handler (struct intr_frame *);
static struct lock global_file_lock;
static size_t fd_count = 2;

static
bool is_valid_args(uint32_t *args, int number_args) {
  // returns true if valid, false otherwise
  if (!(is_user_vaddr(args) && is_user_vaddr(args + 4 * number_args - 1))) {
    return false;
  }

  uint32_t *active_pd = thread_current()->pagedir;

  void *pd_beginning = pagedir_get_page(active_pd, args);
  void *pd_end = pagedir_get_page(active_pd, args + 4 * number_args - 1);
  
  return (pd_beginning != NULL && pd_end != NULL);
}

static 
bool is_valid_file(const char *file) {
  // returns whether the file is valid or not
  if (file == NULL) {
    return false;
  } 

  if (!is_valid_args((uint32_t *) file, 1)) {
    return false;
  }

  return true;
}

static void 
exit(int exit_status) {
  printf ("%s: exit(%d)\n", (char *) &thread_current ()->name, exit_status);
  thread_exit_with_status (exit_status);
}

static void
exit_file_call(int exit_status) {
  lock_release(&global_file_lock);
  exit(exit_status);
}

static struct file *
get_file_from_fd(int fd) {
  struct thread *t = thread_current();
  struct list *l = &t->fd_map;
  struct file *returned_file = NULL;

  lock_acquire(&t->fd_lock);
  thread_fd_t *w;
  for (struct list_elem *e = list_begin(l); e->next != NULL; e = e->next) {
    w = list_entry(e, thread_fd_t, elem);
    if (w->fd == fd) {
      returned_file = w->f;
      break;
    }
  }
  lock_release(&t->fd_lock);
  return returned_file;
}

static void
remove_file(int fd) {
  struct thread *t = thread_current();
  
  lock_acquire(&t->fd_lock);
  struct list *l = &t->fd_map;
  thread_fd_t *w;
  for (struct list_elem *e = list_begin(l); e->next != NULL; e = e->next) {
    w = list_entry(e, thread_fd_t, elem);
    if (w->fd == fd) {
      list_remove(e);
      free(w);
      break;
    }
  }
  lock_release(&t->fd_lock);
  return;
}

void
syscall_init (void)
{
  lock_init(&global_file_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static int
create(const char * file, unsigned initial_size){
  if (!is_valid_file(file)) {
    exit_file_call(-1);
  }
  return filesys_create(file, initial_size);
}

static bool
remove(const char * file) {
  return filesys_remove(file);
}

static int
open(const char * file) {
  if (!is_valid_file(file)) {
    exit_file_call(-1);
  }
  struct file *f = filesys_open(file);
  if (f == NULL) {
    return -1;
  }
  thread_fd_t *fd = malloc(sizeof(thread_fd_t));
  if (fd == NULL) {
    file_close(f);
    return -1;
  }

  fd->fd = fd_count;
  fd->f = f;
  struct thread *t = thread_current();

  lock_acquire(&t->fd_lock);

  struct list *l = &thread_current()->fd_map;
  list_push_front(l, &fd->elem);

  lock_release(&t->fd_lock);

  fd_count++;
  return fd_count-1;
}

static int
filesize(int fd) {
  struct file *f = get_file_from_fd(fd);
  return file_length(f);
}

static int 
read(int fd, void *buffer, int size) {
  if (!is_valid_file(buffer)) {
    exit_file_call(-1);
  }
  struct file *file_ = get_file_from_fd(fd);
  if (file_ != NULL) {
    int read_bytes = file_read(file_, buffer, size);
    return read_bytes;
  }
  return -1;
}

static int
write(int fd, void *buffer, int size) {
  if (!is_valid_file(buffer)) {
    exit_file_call(-1);
  }

  struct file *file_ = get_file_from_fd(fd);
  if (file_ != NULL) {
    int written_bytes = file_write(file_, buffer, size);
    return written_bytes;
  }
  return 0;
}

static void
seek(int fd, int new_pos) {
  struct file *file_ = get_file_from_fd(fd);
  file_seek(file_, new_pos);
}

static int
tell(int fd) {
  struct file *file_ = get_file_from_fd(fd);
  return file_tell(file_);
}

static void
close(int fd) {
  struct file *file_ = get_file_from_fd(fd);
  file_close(file_);
  remove_file(fd);
}

void
close_thread_fd(thread_fd_t *fd) {
  close(fd->fd);
}

static void
file_operation_handler(struct intr_frame *f) {
  uint32_t* args = ((uint32_t*) f->esp);
  lock_acquire(&global_file_lock);
  switch (args[0]) {
    // LUKE AND CHRIS

    case SYS_CREATE:
      if (!is_valid_args(args, 3)) {
        exit_file_call(-1);
      }
      f->eax = create((char *) args[1], (unsigned int) args[2]);
      break;                /* Create a file. */
    case SYS_REMOVE:
      if (!is_valid_args(args, 2)) {
        exit_file_call(-1);
      }
      f->eax = remove((char *) args[1]);
      break;                 /* Delete a file. */
    case SYS_OPEN:
      if (!is_valid_args(args, 2)) {
        exit_file_call(-1);
      }
      f->eax = open((char *) args[1]);
      break;                   /* Open a file. */
    case SYS_FILESIZE:
      if (!is_valid_args(args, 2)) {
        exit_file_call(-1);
      }
      f->eax = filesize((int) args[1]);
      break;               /* Obtain a file's size. */

    // BEN AND DIEGO

    case SYS_READ: {
      if (!is_valid_args(args, 4)) {
        exit_file_call(-1);
      }
      int size = args[3];
      void *buffer = (void *)args[2];
      int fd = args[1];
      f->eax = read(fd, buffer, size);
      break;
    }                  /* Read from a file. */
    case SYS_WRITE: {
      if (!is_valid_args(args, 4)) {
        exit_file_call(-1);
      }

      int size = args[3];
      void *buffer = (void *)args[2];
      int fd = args[1];
      if (fd == 1){
        printf("%.*s", size, (char *) buffer);
      } else {
        f->eax = write(fd, buffer, size);
      }
      break;
    }                  /* Write to a file. */
    case SYS_SEEK: {
      if (!is_valid_args(args, 3)) {
        exit_file_call(-1);
      }

      int new_pos = args[2];
      int fd = args[1];
      seek(fd, new_pos);
      break;            /* Change position in a file. */
    }
    case SYS_TELL: {
      if (!is_valid_args(args, 2)) {
        exit_file_call(-1);
      }
      int fd = args[1];
      f->eax = tell(fd);
      break;
    }                /* Report current position in a file. */
    case SYS_CLOSE: {
      if (!is_valid_args(args, 2)) {
        exit_file_call(-1);
      }
      int fd = args[1];
      close(fd);
      break;
    }
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

  bool valid = is_valid_args(args, 1); // check the first arg here

  if (!valid || args[0] == SYS_EXIT) {
    int exit_status = 0;

    if (!valid) {
      exit_status = -1;
    } else {
      exit_status = args[1];
    }

    f->eax = exit_status;
    exit(exit_status);

  } else if (args[0] == SYS_PRACTICE) {
    f->eax = args[1] + 1;
    return;
  } else if (args[0] == SYS_HALT) {
    shutdown_power_off();
    return;
  } else if (args[0] == SYS_EXEC) {
    if (!is_valid_file(args[1])) {
      exit(-1);
    }
    f->eax = process_execute((char *)args[1]);
    return;
  } else if (args[0] == SYS_WAIT) {

    if (!is_valid_args(args, 2)) {
      exit(-1);
    }
    f->eax = process_wait((int) args[1]);

    return;
  } else { // ITS FILESYSTEM CALL
    file_operation_handler(f);
  }
}