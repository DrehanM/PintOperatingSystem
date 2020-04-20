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
    struct inode_disk data;
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

// writes buffer into c->data. buffer must be size BLOCK_SECTOR_SIZE
void cache_write(block_sector_t sector_idx, void *buffer) {
  struct cached_sector *cs = get_cached_sector(sector_idx);
  char *buff = buffer;
  for (int i = 0; i < BLOCK_SECTOR_SIZE; i++) {
    cs->data[i] = buff[i];
  }
  cs->dirty = 1;
  lock_release(&cs->sector_lock);
}

// writes c->data into buffer. buffer must be atleast size BLOCK_SECTOR_SIZE
void cache_read(block_sector_t sector_idx, void *buffer) {
  struct cached_sector *cs = get_cached_sector(sector_idx);
  char *buff = buffer;
  for (int i = 0; i < BLOCK_SECTOR_SIZE; i++) {
    buff[i] = cs->data[i];
  }
  lock_release(&cs->sector_lock);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);

  struct inode_disk *disk_inode  = NULL;
  struct indirect *doubly_indirect_ptr = NULL;
  struct indirect *indirect_ptr = NULL;

  cache_read(inode->sector, disk_inode);
  
  
  if (pos < disk_inode->length) {
    int level1_position = (pos / BLOCK_SECTOR_SIZE) / NUM_POINTERS;
    int level2_position = (pos / BLOCK_SECTOR_SIZE) % NUM_POINTERS;

    cache_read(disk_inode->indirect_ptr_idx, doubly_indirect_ptr);
    cache_read(doubly_indirect_ptr->ptrs[level1_position], indirect_ptr);

    return indirect_ptr->ptrs[level2_position];
  } else { 
    return -1;
  }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */

/* Initializes the inode module. */
void
inode_init (void) {
  list_init (&open_inodes);
  lock_init(&open_inodes_lock);

  list_init (&buffer_cache);
  lock_init(&buffer_cache_lock);
}

/* Allocates and appends a new data sector to the end of the file */
/* If a level 2 indirect pointer is full, creates a new indirect level 2 indirect pointer */
/* Returns 0 on failure */
block_sector_t add_sector_to_file(block_sector_t sector) {
  struct inode_disk *disk_inode = NULL;
  struct indirect *doubly_indirect_ptr = NULL;
  struct indirect *indirect_ptr = NULL;
  static char zeros[BLOCK_SECTOR_SIZE];

  cache_read(sector, disk_inode);
  
  if (disk_inode->length + BLOCK_SECTOR_SIZE >= MAX_FILE_SIZE) {
    return 0;
  }

  cache_read(disk_inode->indirect_ptr_idx, doubly_indirect_ptr);

  int level1_position = (disk_inode->length / BLOCK_SECTOR_SIZE) / NUM_POINTERS;
  int level2_position = (disk_inode->length / BLOCK_SECTOR_SIZE) % NUM_POINTERS;

  if (level2_position == 0) { // We must create a new level 2 indirect pointer node in the doubly indirect pointer array
    indirect_ptr = calloc(1, sizeof *indirect_ptr);
    
    if (free_map_allocate(1, &doubly_indirect_ptr->ptrs[level1_position])) {
      cache_write(disk_inode->indirect_ptr_idx, doubly_indirect_ptr);
    } else {
      return 0;
    }

  } else { // Else, we pull the next free level 2 pointer to point to the new sector
    cache_read(doubly_indirect_ptr->ptrs[level2_position], indirect_ptr);
  }

  if (!free_map_allocate(1, &indirect_ptr->ptrs[level2_position]))
    return 0;

  disk_inode->length += BLOCK_SECTOR_SIZE;
    
  cache_write(doubly_indirect_ptr->ptrs[level1_position], indirect_ptr);
  cache_write(indirect_ptr->ptrs[level2_position], zeros);

  return indirect_ptr->ptrs[level2_position];
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  struct indirect *doubly_indirect_ptr = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  doubly_indirect_ptr = calloc(1, sizeof *doubly_indirect_ptr);
  if (disk_inode != NULL && doubly_indirect_ptr != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC; 
      
      if (free_map_allocate(1, &disk_inode->indirect_ptr_idx))
        {
          cache_write(disk_inode->indirect_ptr_idx, doubly_indirect_ptr);
          if (sectors > 0) {
            size_t i;
            block_sector_t new_sector;
            for (i = 0; i < sectors; i++) {
             new_sector = add_sector_to_file(disk_inode);
             if (new_sector == 0) {
               free (disk_inode);
               return false;
             }
            }
          }
          cache_write (sector, disk_inode);
          success = true;
        }
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
  lock_init(&(inode->l));
  cache_read (inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    lock_acquire(&(inode->l));
    inode->open_cnt++;
    lock_release(&(inode->l));
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

void free_all_data_sectors(struct inode *inode) {
  struct inode_disk *disk_inode = NULL;
  struct indirect *doubly_indirect_ptr = NULL;
  struct indirect *indirect_ptr = NULL;
  cache_read(inode->sector, disk_inode);
  cache_read(disk_inode->indirect_ptr_idx, doubly_indirect_ptr);

  for (int i = 0; i < NUM_POINTERS && doubly_indirect_ptr->ptrs[i] != 0; i++) {
    cache_read(doubly_indirect_ptr->ptrs[i], indirect_ptr);
    for (int j = 0; j < NUM_POINTERS && indirect_ptr->ptrs[j] != 0; j++) {
      free_map_release(indirect_ptr->ptrs[j], 1);
    }
    free_map_release(doubly_indirect_ptr->ptrs[i], 1);
  }
  free_map_release(disk_inode->indirect_ptr_idx, 1);
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
  lock_acquire(&(inode->l));
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
      lock_release(&(inode->l)); // Kinda redundant since we free anyway
      free (inode);
    } else {
      lock_release(&(inode->l));
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  lock_acquire(&(inode->l));
  ASSERT (inode != NULL);
  inode->removed = true;
  lock_release(&(inode->l));
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          cache_read(sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          cache_read(sector_idx, bounce); // TODO: REMOVE THE BOUNCE
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);  
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

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

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          cache_write (sector_idx, buffer + bytes_written);
        }
      else
        {
          /* We need a bounce buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) {
            cache_read (sector_idx, bounce);
          } else {
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          }
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          cache_write (sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{ 
  lock_acquire(&(inode->l));
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_release(&(inode->l));
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  lock_acquire(&(inode->l));
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  lock_release(&(inode->l));
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
