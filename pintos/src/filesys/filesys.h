#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */

#define NAME_MAX 14

/* Block device that contains the file system. */
struct block *fs_device;

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size, bool isdir);
void *filesys_open (const char *name, bool *isdir);
bool filesys_remove (const char *name);
int get_next_part (char part[NAME_MAX + 1], const char **srcp);
struct dir *get_last_dir(const char *fp);
bool get_filename_from_path(const char *fp, char name[NAME_MAX + 1]);
bool verify_filepath (const char *fp, struct dir *dir, struct inode **inode);

#endif /* filesys/filesys.h */
