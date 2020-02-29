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

static void syscall_handler (struct intr_frame *);
static struct lock global_file_lock;
static size_t fd_count = 2;

// Returns 0 if all necessary arguments of the syscall are in valid userspace memory, are mapped in memory, and are not null pointers.
// Returns 1 if any required argument for the syscall is null.
// Returns 2 if any of the arguments are fully or partially in unmapped memory.
// Returns 3 if any arguments are fully or partially outside of user virtual address space.
static int in_userspace_and_notnull(uint32_t *args) {
  uint32_t *pd = thread_current()->pagedir;

  if (!is_user_vaddr(&args[0]) || !is_user_vaddr(&args[0] + 3)) {
    return 3;
  }

  if (pagedir_get_page(pd, &args[0]) == NULL 
  || pagedir_get_page(pd, &args[0] + 3) == NULL) {
    return 2;
  }

  if (args[0] == NULL) {
    return 1;
  }


  switch(args[0]) {
    case SYS_HALT:
    case SYS_EXIT:
      break;
    case SYS_TELL:
    case SYS_CLOSE:
    case SYS_FILESIZE:
      if (!is_user_vaddr(&args[1]) || !is_user_vaddr(&args[1] + 3)) {
        return 3;
      } else if (pagedir_get_page(pd, &args[1]) == NULL 
          || pagedir_get_page(pd, &args[1] + 3) == NULL) {
        return 2;
      } else {
        break;
      }
    case SYS_PRACTICE:
    case SYS_EXEC:
    case SYS_WAIT:
    case SYS_REMOVE:
    case SYS_OPEN:
    case SYS_MUNMAP:
    case SYS_CHDIR:
    case SYS_MKDIR:
    case SYS_ISDIR:
    case SYS_INUMBER:
      if (args[1] == NULL) {
        return 1;
      } else if (!is_user_vaddr(&args[1]) || !is_user_vaddr(&args[1] + 3)) {
        return 3;
      } else if (pagedir_get_page(pd, &args[1]) == NULL 
          || pagedir_get_page(pd, &args[1] + 3) == NULL) {
        return 2;
      } else {
        break;
      }
    case SYS_CREATE:
      if (args[1] == NULL) {
        return 1;
      } else if (!is_user_vaddr(&args[2]) || !is_user_vaddr(&args[2] + 3)) {
        return 3;
      } else if (pagedir_get_page(pd, &args[2]) == NULL 
          || pagedir_get_page(pd, &args[2] + 3) == NULL) {
        return 2;
      } else {
        break;
      }
    case SYS_SEEK:
      if (!is_user_vaddr(&args[2]) || !is_user_vaddr(&args[2] + 3)) {
        return 3;
      } else if (pagedir_get_page(pd, &args[2]) == NULL 
          || pagedir_get_page(pd, &args[2] + 3) == NULL) {
        return 2;
      } else {
        break;
      }
    case SYS_MMAP:
    case SYS_READDIR:
      if (args[1] == NULL || args[2] == NULL) {
        return 1;
      } else if (!is_user_vaddr(&args[2]) || !is_user_vaddr(&args[2] + 3)) {
        return 3;
      } else if (pagedir_get_page(pd, &args[2]) == NULL 
          || pagedir_get_page(pd, &args[2] + 3) == NULL) {
        return 2;
      } else {
        break;
      }
    case SYS_WRITE:
    case SYS_READ:
      if (args[2] == NULL) {
        return 1;
      } else if (!is_user_vaddr(&args[3]) || !is_user_vaddr(&args[3] + 3)) {
        return 3;
      } else if (pagedir_get_page(pd, &args[3]) == NULL 
          || pagedir_get_page(pd, &args[3] + 3) == NULL) {
        return 2;
      } else {
        break;
      }
    default:
        break;
  }
  return 0;
}

static int validate_buffer(void *ptr, size_t size, uint32_t *pd) {
  while (size > 0) {
    if (!is_user_vaddr(ptr)) {
      return 2;
    }
    size_t bytes_validated;
    size_t page_remaining = ptr - pagedir_get_page(pd, ptr);
    if (page_remaining > size) {
      bytes_validated = size;
    } else {
      bytes_validated = page_remaining;
    }
    ptr += bytes_validated;
    size -= bytes_validated;
  }
  return 0;
}

// Returns 0 if syscall is not handling a file or buffer OR if the parameter files/buffers are in valid user space, mapped correctly, and not null
// Returns 1 if the file or buffer is a null pointer.
// Returns 2 if the file or buffer points to unmapped memory.
// Returns 3 if the file or buffer points to memory outside of user virtual address space.
static int is_valid_file_or_buffer(uint32_t *args) {
  uint32_t *pd = thread_current()->pagedir;

  //precondition: args and args addresses have been verified
  switch (args[0]) {
    case SYS_EXEC:
    case SYS_CREATE:
    case SYS_REMOVE:
    case SYS_OPEN:
      if (args[1] == NULL)
        return 1;
      if (!is_user_vaddr(args[1]) || !is_user_vaddr(args[1] + strlen(args[1])))
        return 3;
      if (pagedir_get_page(pd, args[1]) == NULL || pagedir_get_page(pd, args[1] + strlen(args[1])) == NULL)
        return 2;
      break;
    case SYS_READ:
    case SYS_WRITE:
      if (args[2] == NULL) {
        return 1;
      }
      if (!is_user_vaddr((void *) args[2])) {
        return 3;
      }
      return validate_buffer((void *) args[2], args[3], pd);
    default:
      break;
    }
  return 0;
}

static int validate_syscall_args(uint32_t* args) {
	uint32_t error_code;
	error_code = in_userspace_and_notnull(args);
	if (error_code > 0) {return error_code;}
	error_code = is_valid_file_or_buffer(args);
	if (error_code > 0) {return error_code;}
	return 0;
}

typedef struct thread_fd {
  int fd;
  struct file *f;
  struct list_elem elem;
} thread_fd_t;

static struct file *
get_file_from_fd(int fd) {
  struct list l = thread_current()->fd_map;
  thread_fd_t *w;
  for (struct list_elem *e = list_begin(&l); e->next != NULL; e = e->next) {
    w = list_entry(e, thread_fd_t, elem);
    if (w->fd == fd) {
      return w->f;
    }
  }
  return NULL;
}

static void
remove_file(int fd) {
  struct list l = thread_current()->fd_map;
  thread_fd_t *w;
  for (struct list_elem *e = list_begin(&l); e->next != NULL; e = e->next) {
    w = list_entry(e, thread_fd_t, elem);
    if (w->fd == fd) {
      list_remove(e);
      return;
    }
  }
  return;
}

void
syscall_init (void)
{
  lock_init(&global_file_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static bool
create(const char * file, unsigned initial_size){
  return filesys_create(file, initial_size);
}

static bool
remove(const char * file) {
  return filesys_remove(file);
}

static int
open(const char * file) {
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
  struct list l = thread_current()->fd_map;
  list_push_back(&l, &fd->elem);
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
  struct file *file_ = get_file_from_fd(fd);
  if (file_ != NULL) {
    int read_bytes = file_read(file_, buffer, size);
    return read_bytes;
  }
  return -1;
}

static int
write(int fd, void *buffer, int size) {
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

static void
file_operation_handler(struct intr_frame *f) {
  uint32_t* args = ((uint32_t*) f->esp);
  lock_acquire(&global_file_lock);
  switch (args[0]) {
    // LUKE AND CHRIS

    case SYS_CREATE:
      f->eax = create((char *) args[1], (unsigned int) args[2]);
      break;                /* Create a file. */
    case SYS_REMOVE:
      f->eax = remove((char *) args[1]);
      break;                 /* Delete a file. */
    case SYS_OPEN:
      f->eax = open((char *) args[1]);
      break;                   /* Open a file. */
    case SYS_FILESIZE:
      f->eax = filesize((int) args[1]);
      break;               /* Obtain a file's size. */

    // BEN AND DIEGO

    case SYS_READ: {
      int size = args[3];
      void *buffer = (void *)args[2];
      int fd = args[1];
      f->eax = read(fd, buffer, size);
      break;
    }                  /* Read from a file. */
    case SYS_WRITE: {
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
      int new_pos = args[2];
      int fd = args[1];
      seek(fd, new_pos);
      break;            /* Change position in a file. */
    }
    case SYS_TELL: {
      int fd = args[1];
      f->eax = tell(fd);
      break;
    }                /* Report current position in a file. */
    case SYS_CLOSE: {
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

  /* printf("System call number: %d\n", args[0]); */

  if (validate_syscall_args(args) > 0) {
    f->eax = -1;
    return;
  }

  if (args[0] == SYS_EXIT) {
    f->eax = args[1];
    printf ("%s: exit(%d)\n", (char *) &thread_current ()->name, args[1]);
    thread_exit ();
  } else if (args[0] == SYS_PRACTICE) {
    f->eax = args[1] + 1;
    return;
  } else if (args[0] == SYS_HALT) {
    shutdown_power_off();
    return;
  } else if (args[0] == SYS_EXEC) {
    return;
  } else if (args[0] == SYS_WAIT) {
    return;
  } else { // ITS FILESYSTEM CALL
    file_operation_handler(f);
  }
}