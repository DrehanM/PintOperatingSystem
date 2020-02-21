#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);
static struct lock global_file_lock;
static struct fd_count = 2;


void
syscall_init (void)
{
  lock_init(&global_file_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
file_operation_handler(struct intr_frame *f) {
  uint32_t* args = ((uint32_t*) f->esp);
  lock_acquire(&global_file_lock);
  switch (args[0]) {
    // LUKE AND CHRIS
    
    case SYS_CREATE:
      break;                /* Create a file. */
    case SYS_REMOVE:
      break;                 /* Delete a file. */
    case SYS_OPEN:
      break;                   /* Open a file. */
    case SYS_FILESIZE:
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

  if (args[0] == SYS_EXIT)
    {
      f->eax = args[1];
      printf ("%s: exit(%d)\n", &thread_current ()->name, args[1]);
      thread_exit ();
    }
  if (args[0] == SYS_WRITE) { // IF ANYTHING TO DO WITH FILESYSTEM



  }
}
