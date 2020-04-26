#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define NUM_POINTERS 128
#define MAX_FILE_SIZE 512 * 128 * 128

static struct list buffer_cache; 
static struct lock buffer_cache_lock;

static struct list open_inodes;
static struct lock open_inodes_lock;


/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk {
    block_sector_t indirect_ptr_idx;    /* Sector index of indirect pointer. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[125];               /* Not used. */
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode {
    struct list_elem elem;              /* Element in inode list. */
    struct lock l; 			/* Acquire while changing inode  */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */

    size_t active_writers;
    size_t active_readers;
    size_t waiting_writers;
    size_t waiting_readers;
    struct condition ok_to_read;
    struct condition ok_to_write;
};

struct indirect {
  block_sector_t ptrs[NUM_POINTERS];
};

/* */
struct cached_sector {
	block_sector_t sector_idx; // check against when we go through the cache
	struct list_elem elem; 
	bool dirty; 
	struct lock sector_lock; // acquire if currently reading/writing to this sector
	char data[BLOCK_SECTOR_SIZE]; // of size BLOCK_SECTOR_SIZE, could be inodedisk or data block, up to caller to cast to correct one
};

/* Gets the cached_sector from buffer_cache. 
   If sector_idx isnt present, then we evict the last sector and pull in sector_idx from memory. 
   During eviction, we write back to memory using block_write if the dirty bit is true. 
   Aquires the sectors lock and returns a pointer to the sector. 
   Callers of this function must release sectors lock after done using. */
struct cached_sector *get_cached_sector(block_sector_t sector_idx) {
  // look for sector_idx in list
  struct list_elem *e;
  struct cached_sector *cs;

  lock_acquire(&buffer_cache_lock);

  if (!list_empty(&buffer_cache)) { // iterate through the list and look for matching sector
    for (e = list_front(&buffer_cache); e->next != NULL; e = list_next(e)) {
      cs = list_entry(e, struct cached_sector, elem);
      if (cs->sector_idx == sector_idx) {
        lock_release(&buffer_cache_lock);
        lock_acquire(&cs->sector_lock);
        // make sure that cs hasn't changed because of an eviction
        if (cs->sector_idx == sector_idx) {
          return cs;
        } 
        // if it has we need to iterate through the list again to make sure it hasnt been pulled in since
        lock_release(&cs->sector_lock);
        return get_cached_sector(sector_idx);
      }
    }
  }

  // didnt find item in buffer
  struct cached_sector *new_cs;

  // if theres still size left, we can just create a cached_sector
  if (list_size(&buffer_cache) < 64) {
    new_cs = malloc(sizeof(struct cached_sector));
    lock_init(&new_cs->sector_lock);
    lock_acquire(&new_cs->sector_lock);
  } else { // otherwise we grab the last item in the list
    new_cs = list_entry(list_back(&buffer_cache), struct cached_sector, elem);
    lock_acquire(&new_cs->sector_lock); // we block until this sector is no longer in use
    list_remove(&new_cs->elem);
    if (new_cs->dirty) { // we need to write back to disk
      block_write (fs_device, new_cs->sector_idx, new_cs->data);
    }
  }

  block_read(fs_device, sector_idx, new_cs->data);
  new_cs->sector_idx = sector_idx;
  new_cs->dirty = 0;

  list_push_front(&buffer_cache, &new_cs->elem);
  lock_release(&buffer_cache_lock);
  return new_cs;
}

// Called from filesys_done
void write_all_dirty_sectors() {
  struct cached_sector *cs;
  struct list_elem *e;
  lock_acquire(&buffer_cache_lock);
  
  for (e = list_front(&buffer_cache); e->next != NULL; e = list_next(e)) {
    cs = list_entry(e, struct cached_sector, elem);
    if (cs->dirty) {
      block_write (fs_device, cs->sector_idx, cs->data);
      cs->dirty = 0;
    }
  }

  lock_release(&buffer_cache_lock);
}

// writes buffer into c->data. buffer must be size SIZE.
// Only called when we don't want to write the whole block sector (e.g. inode_write_at)
// Also called in cache_write with args SIZE = BLOCK_SECTOR_SIZE and SECTOR_OFS = 0
void cache_write_with_size_and_offset(block_sector_t sector_idx, void *buffer, size_t size, size_t sector_ofs) {
  ASSERT(sector_ofs + size <= BLOCK_SECTOR_SIZE);
  struct cached_sector *cs = get_cached_sector(sector_idx);
  char *buff = buffer;
  for (size_t i = 0; i < size; i++) {
    cs->data[i + sector_ofs] = buff[i];
  }
  cs->dirty = 1;
  lock_release(&cs->sector_lock);
}

void cache_write(block_sector_t sector_idx, void *buffer) {
  cache_write_with_size_and_offset(sector_idx, buffer, BLOCK_SECTOR_SIZE, 0);
}

// writes c->data into buffer. buffer must be atleast size SIZE.
// Only called when we don't want to read the whole block sector (e.g. inode_read_at)
// Also called in cache_read with args SIZE = BLOCK_SECTOR_SIZE and SECTOR_OFS = 0
void cache_read_with_size_and_offset(block_sector_t sector_idx, void *buffer, size_t size, size_t sector_ofs) {
  ASSERT(sector_ofs + size <= BLOCK_SECTOR_SIZE);
  struct cached_sector *cs = get_cached_sector(sector_idx);
  char *buff = buffer;
  for (size_t i = 0; i < size; i++) {
    buff[i] = cs->data[i + sector_ofs];
  }
  lock_release(&cs->sector_lock);
}

// 
void cache_read(block_sector_t sector_idx, void *buffer) {
  cache_read_with_size_and_offset(sector_idx, buffer, BLOCK_SECTOR_SIZE, 0);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);

  struct inode_disk *disk_inode = calloc(1, sizeof(struct inode_disk));
  
  cache_read(inode->sector, disk_inode);
  
  if (pos < disk_inode->length) {
    struct indirect *doubly_indirect_ptr = calloc(1, sizeof(struct indirect));
    struct indirect *indirect_ptr = calloc(1, sizeof(struct indirect));

    int level1_position = (pos / BLOCK_SECTOR_SIZE) / NUM_POINTERS;
    int level2_position = (pos / BLOCK_SECTOR_SIZE) % NUM_POINTERS;

    cache_read(disk_inode->indirect_ptr_idx, doubly_indirect_ptr);
    cache_read(doubly_indirect_ptr->ptrs[level1_position], indirect_ptr);

    block_sector_t result = indirect_ptr->ptrs[level2_position];

    free(disk_inode);
    free(doubly_indirect_ptr);
    free(indirect_ptr);
    return result;
  }
    
  free(disk_inode);
  return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */

/* Initializes the inode module and buffer cache. */
void
inode_init (void) {
  list_init (&open_inodes);
  lock_init(&open_inodes_lock);

  list_init (&buffer_cache);
  lock_init(&buffer_cache_lock);
}

/* Allocates and appends a new data sector to the end of the file */
/* Creates indirect pointers as needed. Returns sector number of allocated data sector. */
/* Returns 0 on failure */
bool add_sector_to_file(struct inode_disk *disk_inode, block_sector_t *sector) {
  static char zeros[BLOCK_SECTOR_SIZE];
  if (free_map_allocate(1, sector)) {
    cache_write(*sector, zeros);
    return true;
  }
  return false;
}

/* Resizes file. Rolls back actions if allocation fails. */
bool inode_resize(struct inode_disk *disk_inode, off_t size) {
  struct indirect *doubly_indirect_ptr = calloc(1, sizeof(struct indirect));
  struct indirect *indirect_ptr = calloc(1, sizeof(struct indirect));

  if (disk_inode->indirect_ptr_idx == 0 && size == 0) {
    free(doubly_indirect_ptr);
    free(indirect_ptr);
    return true;
  }

  size_t original_length = disk_inode->length;
  
  if (disk_inode->indirect_ptr_idx == 0) {
    if (!add_sector_to_file(disk_inode, &disk_inode->indirect_ptr_idx)) {
      free(doubly_indirect_ptr);
      free(indirect_ptr);
      inode_resize(disk_inode, original_length);
      return false;
    }
  }

  cache_read(disk_inode->indirect_ptr_idx, doubly_indirect_ptr);
  
  // Loop through level 1 pointers
  for (int i = 0; i < NUM_POINTERS; i++) {  
    
    // Allocate a singly indirect sector if we need one
    if (size > i * (BLOCK_SECTOR_SIZE * NUM_POINTERS) && doubly_indirect_ptr->ptrs[i] == 0) {
      if (!add_sector_to_file(disk_inode, &doubly_indirect_ptr->ptrs[i])) {
        free(doubly_indirect_ptr);
        free(indirect_ptr);
        inode_resize(disk_inode, original_length);
        return false;
      }
    }

    if (doubly_indirect_ptr->ptrs[i] != 0) {
    
      cache_read(doubly_indirect_ptr->ptrs[i], indirect_ptr);
    // Loop through the level 2 pointers
      for (int j = 0; j < NUM_POINTERS; j++) {        
        // Allocate a data sector if we need one
        if (size > i * (BLOCK_SECTOR_SIZE * NUM_POINTERS) + j * (BLOCK_SECTOR_SIZE) && indirect_ptr->ptrs[j] == 0) {
          if (!add_sector_to_file(disk_inode, &indirect_ptr->ptrs[j])) {
            free(doubly_indirect_ptr);
            free(indirect_ptr);
            inode_resize(disk_inode, original_length);
            return false;
          }
        }
        
        // Delete a data sector if the file is too large
        if (size <= i * (BLOCK_SECTOR_SIZE * NUM_POINTERS) + j * (BLOCK_SECTOR_SIZE) && indirect_ptr->ptrs[j] != 0) {
          free_map_release(indirect_ptr->ptrs[j], 1);
          indirect_ptr->ptrs[j] = 0;
        }
        
      }
      // Commit the singly indirect pointer into disk
      cache_write(doubly_indirect_ptr->ptrs[i], indirect_ptr);
    }
    
    // Delete a singly indirect pointer if the file is too large
    if (size <= i * (BLOCK_SECTOR_SIZE * NUM_POINTERS) && doubly_indirect_ptr->ptrs[i] != 0) {
      free_map_release(doubly_indirect_ptr->ptrs[i], 1);
      doubly_indirect_ptr->ptrs[i] = 0;
    }
  }

  cache_write(disk_inode->indirect_ptr_idx, doubly_indirect_ptr);

  if (size == 0) {
    free_map_release(disk_inode->indirect_ptr_idx, 1);
    disk_inode->indirect_ptr_idx = 0;
  }

  disk_inode->length = size;

  free(doubly_indirect_ptr);
  free(indirect_ptr);
  return true;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = calloc(1, sizeof(struct inode_disk));
  struct indirect *doubly_indirect_ptr = calloc(1, sizeof(struct indirect));
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  if (disk_inode != NULL && doubly_indirect_ptr != NULL)
    {
      disk_inode->magic = INODE_MAGIC; 
      
      if (free_map_allocate(1, &disk_inode->indirect_ptr_idx))
        {
          cache_write(disk_inode->indirect_ptr_idx, doubly_indirect_ptr);
          if (length > 0) {
             if (!inode_resize(disk_inode, length)) {
               free (doubly_indirect_ptr);
               free (disk_inode);
               return false;
             }
          }
          cache_write (sector, disk_inode);
          success = true;
        }
      free (doubly_indirect_ptr);
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  lock_acquire(&open_inodes_lock);
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          lock_release(&open_inodes_lock);
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  lock_release(&open_inodes_lock);

  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  
  inode->active_writers = 0;
  inode->active_readers = 0;
  inode->waiting_writers = 0;
  inode->waiting_readers = 0;
  
  cond_init(&(inode->ok_to_read));
  cond_init(&(inode->ok_to_write));
  lock_init(&(inode->l));
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL) {
    writer_checkin(inode);
    inode->open_cnt++;
    writer_checkout(inode);
  }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

void free_all_data_sectors(struct inode *inode) {
  struct inode_disk *disk_inode = calloc(1, sizeof(struct inode_disk));
  cache_read(inode->sector, disk_inode);
  inode_resize(disk_inode, 0);
  free(disk_inode);
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  writer_checkin(inode);
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      lock_acquire(&open_inodes_lock);
      list_remove (&inode->elem);
      lock_release(&open_inodes_lock);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_all_data_sectors(inode);
          free_map_release (inode->sector, 1);
        }
      writer_checkout(inode); // Kinda redundant since we free anyway
      free (inode);
    } else {
      writer_checkout(inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  writer_checkin(inode);
  ASSERT (inode != NULL);
  inode->removed = true;
  writer_checkout(inode);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, size_t size, size_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  reader_checkin(inode);

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      if (sector_idx == -1) {
        break;
      }
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      cache_read_with_size_and_offset(sector_idx, buffer + bytes_read, chunk_size, sector_ofs);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
    
  reader_checkout(inode);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, size_t size,
                size_t offset)
{
  
  ASSERT(inode != NULL);
  
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  struct inode_disk *disk_inode = calloc(1, sizeof(struct inode_disk));
  bool is_extension = false;

  reader_checkin(inode);

  if (inode->deny_write_cnt) {
    reader_checkout(inode);
    free(disk_inode);
    return 0;
  }

  reader_checkout(inode);

  writer_checkin(inode);
  if (inode_length(inode) < size + offset) {
      is_extension = true;
      cache_read(inode->sector, disk_inode);
      if (!inode_resize(disk_inode, size + offset)) {
        writer_checkout(inode);
        free(disk_inode);
        return 0;
      }
      cache_write(inode->sector, disk_inode);
  } else {
    writer_checkout(inode);
    reader_checkin(inode);
  }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      
      cache_write_with_size_and_offset(sector_idx, buffer + bytes_written, chunk_size, sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  
  if (is_extension) {
    writer_checkout(inode);
  } else {
    reader_checkout(inode);
  }

  free(disk_inode);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{ 
  writer_checkin(inode);
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  writer_checkout(inode);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  writer_checkin(inode);
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  writer_checkout(inode);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk *disk_inode = calloc(1, sizeof (struct inode_disk));
  cache_read(inode->sector, disk_inode);
  size_t result = disk_inode->length;
  free(disk_inode);
  return result;
}

/* Called when a process wants to read inode data.
 * Required to checkin for the entirety of inode_read_at call */
void reader_checkin(struct inode *inode) {
  lock_acquire(&(inode->l));
  while (inode->active_writers + inode->waiting_writers > 0) {
    inode->waiting_readers++;
    cond_wait(&(inode->ok_to_read), &(inode->l));
    inode->waiting_readers--;
  }
  inode->active_readers++;
  lock_release(&(inode->l));
};

void reader_checkout(struct inode *inode) {
  lock_acquire(&(inode->l));
  inode->active_readers--;
  if (inode->active_readers == 0 && inode->waiting_writers > 0) {
    cond_signal(&(inode->ok_to_write), &(inode->l));
  }
  lock_release(&(inode->l));
};

/* Called when process wants to edit inode data. */
void writer_checkin(struct inode *inode) {
  lock_acquire(&(inode->l));
  while (inode->active_writers + inode->active_readers > 0) {
    inode->waiting_writers++;
    cond_wait(&(inode->ok_to_write), &(inode->l));
    inode->waiting_writers--;
  }
  inode->active_writers++;
  lock_release(&(inode->l));
};

void writer_checkout(struct inode *inode) {
  lock_acquire(&(inode->l));
  inode->active_writers--;
  if (inode->waiting_writers > 0) {
    cond_signal(&(inode->ok_to_write), &(inode->l));
  } else if (inode->waiting_readers > 0) {
    cond_broadcast(&(inode->ok_to_read), &(inode->l));
  }
  lock_release(&(inode->l));
};