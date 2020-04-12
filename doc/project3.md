Design Document for Project 3: File Systems
===========================================

## Group Members

* Luke Dai <luke.dai@berkeley.edu>
* Christopher DeVore <chrisdevore@berkeley.edu>
* Dre Maharachi <dre@berkeley.edu>
* Benjamin Ulrich <udotneb@berkeley.edu>
* Diego Uribe <diego.uribe@berkeley.edu>

# Task 1: Buffer Cache

### Data Structures and Functions
```c
struct list buffer_cache; 
struct lock buffer_cache_lock;

static struct list open_inodes;
struct lock open_inodes_lock;

struct cached_sector {
	block_sector_t sector_idx; // check against when we go through the cache
	block_sector_t inode_idx; // where is the corresponding inode located in memory? If this sector is an inode, then it’s exactly the same as sector_idx
	list_elm elem; 
	bool dirty; 
	struct lock sector_lock; // acquire if currently reading/writing to this sector
	byte data[]; // of size BLOCK_SECTOR_SIZE, could be inodedisk or data block, up to caller to cast to correct one
}

struct inode // 
  {
    struct list_elem elem;              /* Element in inode list. */
    struct lock l; 			/* Acquire before reading or writing to any disk block on this inode, release after */ 
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    int removed;                        /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. Described in pt 2 */
  }; 

// Chage to return sector number based off of our tree of indirects described in pt2
block_sector_t byte_to_sector (const struct inode *inode, off_t pos)

// writes buffer into c->data.
void write_cached_sector(struct cached_sector *c, void *buffer)


// writes c->data into buffer. buffer must be atleast size BLOCK_SECTOR_SIZE
void read_cached_sector(struct cached_sector *c, void *buffer)


// Gets the cached_sector from buffer_cache. If sector_idx isnt present, then we evict the last sector and pull in sector_idx from memory. During eviction, we write back to memory using block_write if the dirty bit is true. Aquires the sectors lock and returns a pointer to the sector. Callers of this function must release sectors lock after done using. 
struct cached_sector *get_cached_sector(block_sector_t sector_idx);


// Change to call get_cached_sector and read_cached_sector. Aquires lock of cached_sector while reading each sector.
off_t inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset);


// change to call get_cached_sector and write_cached_sector. Modifies dirty bit. Aquires lock of cached_sector while writing to each sector.
off_t inode_write_at (struct inode *inode, const void *buffer_, off_t size, off_t offset);


// change to call get_cached_sector. Aquires and releases open_inodes_lock
struct inode *inode_open (block_sector_t sector);

// Aquires and releases open_inodes_lock to avoid race condition incrementing
struct inode *inode_open (block_sector_t sector);

// Aquires and releases open_inodes_lock to avoid race condition removing inode from open_inodes.
void inode_close (struct inode *inode)
```

### Algorithms
`get_cached_sector(block_sector_t sector_idx)`: 
When we look for a `sector_idx` in our cache, we iterate through our `buffer_cache` and check `cached_sector->sector_idx` for equality. If we find a matching index. 

If dont find a matching index, we have to pull in the sector from disk. If there is space in the cache, then we can just push to the front of our `buffer_cache`. If there isn't, then we have to evict from the back of `buffer_cache` and push our new sector to the front. 

Our eviction strategy is as follows: assume we are trying to evict `struct cached_sector a`. We first try to aquire `a->sector_lock`. 

### Synchronization
lock on every inode (`inode->l`): We have a lock on every inode so that we can't be reading and writing to an inode at the same time. We aquire the lock at the beginning of `inode_read_at` and `inode_write_at` and release the lock at the end of those functions. 

lock on open_inodes (`open_inodes_lock`): We have a lock on `open_inodes` to prevent race conditions when changing the `open_inodes`.

lock on buffer_cache (`buffer_cache_lock`): We have a lock on `buffer_cache` to prevent race conditions when changing `buffer_cace`.

lock on each cached_sector (`cached_sector->sector_lock`): We have a lock for each cached_sector to ensure that a `cached_sector` is not evicted when we are currently reading or writing from it. 

### Rationale


