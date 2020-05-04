#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "userprog/syscall.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init (); 
  free_map_init ();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  write_all_dirty_sectors ();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool isdir)
{
  block_sector_t inode_sector = 0;
  struct dir *dir;
  if (thread_current()->cwd != NULL) {
    dir = dir_open(dir_get_inode(thread_current()->cwd));
  } else {
    dir = dir_open_root();
  }
  bool success;

  if (isdir) {
    success = (dir != NULL
            && free_map_allocate (1, &inode_sector)
            && dir_create (inode_sector, initial_size)
            && dir_add(dir, name, inode_sector));
  } else {
  success = (dir != NULL
            && free_map_allocate (1, &inode_sector)
            && inode_create (inode_sector, initial_size)
            && dir_add (dir, name, inode_sector));
  }
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
void *
filesys_open (const char *name, bool *isdir)
{
  struct dir *dir;
  struct inode *inode;

  bool success = verify_filepath(name, dir, &inode);

  if (!success) {
    return NULL;
  }

  dir_close (dir);

  if (is_dir(inode)) {
    *isdir = true;
    return dir_open(inode);
  } else {
    *isdir = false;
    return file_open(inode);
  }
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

/* Provided by CS162 staff.
Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
next call will return the next file name part. Returns 1 if successful, 0 at
end of string, -1 for a too-long file name part. */
int get_next_part (char part[NAME_MAX + 1], const char **srcp) {
  const char *src = *srcp;
  char *dst = part;
  /* Skip leading slashes. If it’s all slashes, we’re done. */
  while (*src == '/')
    src++;
    if (*src == '\0')
      return 0;
  /* Copy up to NAME_MAX character from SRC to DST. Add null terminator. */
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX)
      *dst++ = *src;
    else
      return -1;
    src++;
  }
  *dst = '\0';
  /* Advance source pointer. */
  *srcp = src;
  return 1;
}

/* Verify the validity of the file path and place the target inode in INODE and enclosing dir in DIR.
   Return true on success. */
bool 
verify_filepath (const char *fp, struct dir *dir, struct inode **inode) {
  if (strcmp(fp, "") == 0) {
    return false;
  }
  if (dir == NULL) {
    thread_current()->cwd = dir_open_root();
    dir = thread_current()->cwd;
  } else {
    dir = dir_open(dir_get_inode(thread_current()->cwd));
  }
  if (fp[0] == '/') {
    dir_close(dir);
    dir = dir_open_root();
  }

  char name[NAME_MAX + 1];
  memset(name, 0, NAME_MAX + 1);
  while (get_next_part(name, &fp)) {
    bool success = dir_lookup(dir, name, inode);
    if (success) {
      if (is_dir(*inode)) {
        dir_close(dir);
        dir = dir_open(*inode);
      } else {
        if (get_next_part(name, &fp) == 1) {
          dir_close(dir);
          return false;  //Error if we path doesn't end but we found a non-dir inode with the same name
        }   
        break;  // Break if found the inode
      }
    } else {
      dir_close(dir);
      return false; //Error if a name does not exist in the path
    }
  }

  return true;
}


