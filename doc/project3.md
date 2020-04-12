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


// Gets the cached_sector from buffer_cache. 
// If sector_idx isnt present, then we evict the last sector and pull in sector_idx from memory. 
// During eviction, we write back to memory using block_write if the dirty bit is true. 
// Aquires the sectors lock and returns a pointer to the sector. 
// Callers of this function must release sectors lock after done using. 
struct cached_sector *get_cached_sector(block_sector_t sector_idx);


// change to call get_cached_sector and read_cached_sector. Acquires and releases inode->l.
off_t inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset);


// change to call get_cached_sector and write_cached_sector. Modifies dirty bit. Acquires and releases inode->l.
off_t inode_write_at (struct inode *inode, const void *buffer_, off_t size, off_t offset);


// change to call get_cached_sector. 
struct inode *inode_create (block_sector_t sector, off_t length);

// Aquires and releases open_inodes_lock to avoid race condition incrementing
struct inode *inode_open (block_sector_t sector);

// Acquires and releases open_inodes_lock to avoid race condition removing inode from open_inodes.
void inode_close (struct inode *inode);

// All these functions are changed to acquire and releases inode->l
struct inode *inode_reopen (struct inode *inode);
void inode_close (struct inode *inode);
void inode_remove (struct inode *inode);
void inode_deny_write (struct inode *inode);
void inode_allow_write (struct inode *inode);
```


### Algorithms

#### Getting sectors from the cache
`get_cached_sector(block_sector_t sector_idx)`: 

We first acquire `buffer_cache_lock`.
We iterate through our `buffer_cache` and check `cached_sector->sector_idx` for equality with `sector_idx`. 

If we find a matching index, we then acquire `cached_sector->sector_lock`, push the `cached_sector` to the front of the cache, release `buffer_cache_lock`, and return `cached_sector`.
We dont have to worry about `cached_sector` being evicted because eviction happens while `buffer_cache_lock` is acquired.

If we dont find a matching index, we have to pull in the sector from disk, make it into a `struct cached_sector`, and acquire `cached_sector->sector_lock`. If there is space in the cache, we then just push to the front of our `buffer_cache`. If there isn't, then we have to evict from the back of `buffer_cache` and push our new sector to the front. 

Our eviction strategy is as follows: assume we are trying to evict `struct cached_sector evict`. We acquire `evict->sector_lock` to ensure that no other thread is currently using this sector. We then check to see if `evict->dirty` is set. If it is we must write `evict->data` back to memory using `block_write`. After this is done then we finally evict.

We release the `buffer_cache_lock` and return `cached_sector`.


### Synchronization
lock on every inode (`inode->l`): We have a lock on every inode so that we can't be reading and writing to an inode at the same time. We aquire the lock at the beginning of `inode_read_at` and `inode_write_at` and release the lock at the end of those functions. We also use this lock when we are editing the inode struct members itself. The reason we use the same lock is so that for example we don't close an inode while we are still performing operations to its data.

lock on open_inodes (`open_inodes_lock`): We have a lock on `open_inodes` to prevent race conditions when changing the `open_inodes`.

lock on buffer_cache (`buffer_cache_lock`): We have a lock on `buffer_cache` to prevent race conditions when changing `buffer_cache`. We acquire and release at the call to `get_cached_sector`.

lock on each cached_sector (`cached_sector->sector_lock`): We have a lock for each cached_sector to ensure that a `cached_sector` is not evicted when we are currently reading or writing from it. We acquire the lock when we return the sector in `get_cached_sector`, and its up to the caller to release the lock when they are finished with the sector. The evicting process tries to acquire this lock when its about to evict. 

### Rationale

We set the `buffer_cache` length to be 62 long. Since we use some extra bytes for metadata, this leaves us with 280 bytes extra, which means we can't allocate another sector in the cache.

Before you perform a read or write operation on a cached disk block you need to acquire the inode lock and the sector lock. This prevents concurrent read and writes to the same inode from happening and also solves the problem of evicting a cache block that is currently being written to. 
Concurrent read and writes: before accessing the cache in the functions inode_read_at and inde_write_at the calling process must first acquire the inode lock, this prevents two processes from modifying an inode at the same time or reading from an inode while it is being modified. This issue is solved solely by the inode lock.
However, the inode lock does not solve the issue of evicting a cached block that is currently being written too. Thus, we had to add the sector lock that must be acquired before writing to, reading from, or evicting a sector lock.

Another design that we considered when evicting a block was just checking if the lock of the inode that holds this disk block is currently being held by another process. If this is the case then we would try to acquire the lock before evicting the block. Although this solves the problem it is a very inefficient solution. 


# Task 2: Extensible Files

### Data Structures and Functions
```c
struct inode_disk { // size equal to BLOCK_SECTOR_SIZE
    block_sector_t  indiect_ptr_idx;               /* Point to doubly indirect. */
    off_t length;                     		   /* File size in bytes. */
    unsigned magic;                    		   /* Magic number. */
    uint32_t unused[125];              	   	   /* Not used, just to fill to BLOCK_SECTOR_SIZE */
}

struct indirect { // size equal to BLOCK_SECTOR_SIZE
    block_sector_t[128] ptrs;                      /* Points to other sectors, could be either indirects or data blocks */
}

// Creates a doubly indirect sector. Then calls add_sector_to_file enough times to fullfill length. 
bool inode_create (block_sector_t sector, off_t length);

// Change to go through our indirect tree to find the correct block_sector.
// Makes multiple calls to get_cached_sector to pull in indirect sectors
block_sector_t byte_to_sector (const struct inode *inode, off_t pos);

// Allocates another datablock to the file and adds to the indirect tree
block_sector_t add_sector_to_file(struct inode *inode);

// If offset > inode->inode_disk.length, we allocate sectors of all zero to fill the gap between offset and the eof
off_t inode_write_at (struct inode *inode, void *buffer_, off_t size, off_t offset);

// Edit to not do anything if we try reading beyond the file
off_t inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset);

// Change to free_map_release the whole indirect tree 
void inode_close (struct inode *inode);
```

### Algorithms

`add_sector_to_file`:
Allocates one sector, adds it to the tree structure.
Creates new indirects if the others are full.
Calls `free_map_allocate` with a count of 1. 

`inode_create`: Allocates the doubly indirect sector and assigns it to `disk_inode->indiect_ptr_idx`. 
We figure out how many sectors we need to fill `length`. 
We then call `add_sector_to_file` that many times.

`byte_to_sector`: We find the sector number corresponding to the offset by traversing the tree structure based off the offset.
For example, at 0 offset we will traverse the 0th index of the doubly indirect, and return the 0th index of the indirect for the sector number. At 512 offset we will traverse the 0th index of the doubly indirect, and return the 1st index of the indirect.

`inode_write_at`: The change we make here is while inode->length < offset, we `add_sector_to_file` and fill the sector with zeros. Once we get to the offset we can write as before. If are writing past the end of our file, we call `add_sector_to_file` as many times as the remaining size of the write // 512 and continue.

### Synchronization
This part is operating at the disk level and we always work through the cache. 
We assume our cache is synchronized from part 1, therefore we don't have to do any syhnchronization as here by transitive property.

### Rationale
We only need one level of doubly indirect because of the following. Our one doubly indirect points to 128 2^7 indirects. Each of these indirects points to 128 2^7 data blocks. Each of these data blocks is 512 2^9 bytes. Therefore this supports 2^23 byte files as specified in the spec. 

We chose to always go through a doubly indirect instead of using direct blocks first to avoid edge cases and to avoid having to coalesce direct blocks after running out of space. Ideally in a real OS they would start with direct blocks so small files easily find there sector number without needing 2 extra disk calls.








