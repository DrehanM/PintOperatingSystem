Design Document for Project 3: File Systems
===========================================

## Group Members

* Luke Dai <luke.dai@berkeley.edu>
* Christopher DeVore <chrisdevore@berkeley.edu>
* Dre Mahaarachchi <dre@berkeley.edu>
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

// We also want to write all the dirty cached_sector to disk when in this function
void filesys_done (void);
```


### Algorithms

#### Getting sectors from the cache
`get_cached_sector(block_sector_t sector_idx)`: 

We first acquire `buffer_cache_lock`.
We iterate through our `buffer_cache` and check `cached_sector->sector_idx` for equality with `sector_idx`. 

If we find a matching index, we then acquire `cached_sector->sector_lock`, push the `cached_sector` to the front of the cache, release `buffer_cache_lock`, and return `cached_sector`.
We dont have to worry about `cached_sector` being evicted because eviction happens while `buffer_cache_lock` is acquired.

Else if we dont find a matching index, we have to pull in the sector from disk, make it into a `struct cached_sector`, and acquire `cached_sector->sector_lock`. If there is space in the cache, we then just push to the front of our `buffer_cache`. If there isn't, then we have to evict from the back of `buffer_cache` and push our new sector to the front. 

Our eviction strategy is as follows: assume we are trying to evict `struct cached_sector evict`. We acquire `evict->sector_lock` to ensure that no other thread is currently using this sector. We then check to see if `evict->dirty` is set. If it is we must write `evict->data` back to memory using `block_write`. After this is done then we finally evict.

We release the `buffer_cache_lock` and return `cached_sector`.


### Synchronization
lock on inode (`inode->l`): We use this lock when we are editing the inode struct members in functions: 
`void inode_close (struct inode *inode);`
`void inode_remove (struct inode *inode);`
`void inode_deny_write (struct inode *inode);`
`void inode_allow_write (struct inode *inode);`

lock on open_inodes (`open_inodes_lock`): We have a lock on `open_inodes` to prevent race conditions when changing the `open_inodes`.

lock on buffer_cache (`buffer_cache_lock`): We have a lock on `buffer_cache` to prevent race conditions when changing `buffer_cache`. We acquire and release at the call to `get_cached_sector`.

lock on each cached_sector (`cached_sector->sector_lock`): We have a lock for each cached_sector to ensure that a `cached_sector` is not evicted when we are currently reading or writing from it. We acquire the lock when we return the sector in `get_cached_sector`, and its up to the caller to release the lock when they are finished with the sector. The evicting process tries to acquire this lock when its about to evict. 

### Rationale

We set the `buffer_cache` length to be 62 long. Since we use some extra bytes for metadata, this leaves us with 280 bytes extra, which means we can't allocate another sector in the cache.

Before you perform a read or write operation on a cached disk block you need to acquire the sector lock. This prevents concurrent read and writes to the same sector from happening and also solves the problem of evicting a cache block that is currently being written to. 

We thought about whether using `buffer_cache_lock` was wrong, but thought that this still allows concurrent writes because the actual writing and reading is the meat of the latency, not iterating through the 62 sectors and returning a pointer. 

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

// Creates a doubly indirect sector. Then calls add_sector_to_file enough times to fulfill length. 
bool inode_create (block_sector_t sector, off_t length);

// Change to go through our indirect tree to find the correct block_sector.
// Makes multiple calls to get_cached_sector to pull in indirect sectors
block_sector_t byte_to_sector (const struct inode *inode, off_t pos);

// Allocates another datablock to the file and adds to the indirect tree. If allocation fails it return -1.
block_sector_t add_sector_to_file(struct inode *inode);

// If offset > inode->inode_disk.length, we allocate sectors of all zero to fill the gap between offset and the eof. 
// We will increase the lenght of the inode only after allocation for a new block succeds. If a call to add_sector_to_file fails we will not attempt to allocate any other sectors. This will leave the OS in a consistent state since the length of the inode is only increased after allocation succeeds. 
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

`inode_write_at`: We first acquire `inode->l`. We then check inode->length < offset + size,  and call `add_sector_to_file` and fill the sector with zeros. Once we get to the offset we release `inode->l` write as before. If are writing past the end of our file, we call `add_sector_to_file` as many times as the remaining size of the write // 512 and continue.

### Synchronization
In `inode_write_at`, our use of the lock `inode->l` solves the problem of two processes appending sectors to a file at the same time. This still allows multiple processes to write to different disk sectors of the same file, it just ensures that `add_sector_to_file` isn't called concurrently, since this would cause our indirect tree to be wonky. 

This part is operating at the disk level and we always work through the cache. 
We assume our cache is synchronized from part 1, therefore we don't have to do any syhnchronization as here by transitive property.

### Rationale
We only need one level of doubly indirect because of the following. Our one doubly indirect points to 128 2^7 indirects. Each of these indirects points to 128 2^7 data blocks. Each of these data blocks is 512 2^9 bytes. Therefore this supports 2^23 byte files as specified in the spec. 

We chose to always go through a doubly indirect instead of using direct blocks first to avoid edge cases and to avoid having to coalesce direct blocks after running out of space. Ideally in a real OS they would start with direct blocks so small files easily find their sector number without needing 2 extra disk calls.


## Task 3: Subdirectories

### Data Structures and Functions

```C
struct thread {
    ...
    char *cwd; /* This thread's current working directory */
    ...
}

// Set cwd via malloc calls. cwd = "/" for the first thread on startup.
// All subsequent threads set cwd as follows in thread_create
tid_t thread_create (const char *name, int priority, thread_func *function, void *aux) {
	...
	t->cwd = malloc(sizeof(thread_current()->cwd))
	strlcpy(t->cwd, thread_current()->cwd, sizeof(thread_current()->cwd))
	...
}

/* Abstraction of fd map list for the thread */
typedef struct thread_fd {
    int fd;
    struct file *f;  //valid if fd is a non-dir file. NULL if fd points to a directory
    struct dir *d;   //valid if fd is a directory. NULL if fd points to a file
    struct list_elem elem;
} thread_fd_t;

// Start from root or cwd (root if preceded by "/")
// Iteratively read file path part and verify if part exists using get_next_part() and dir_lookup()
//Return true if fp is valid, false otherwise
bool verify_filepath(const char *fp);

// Extract part of a file name as described in proj3 spec. Used by verify_filepath.
static int get_next_part (char part[NAME_MAX + 1], const char **srcp);

// Calls dir_create in place of inode_create if isdir == 1
// All calls to this function will be changed appropriately
bool filesys_create (const char *name, off_t initial_size, bool isdir);

// Invoke verify_filepath(dir)
// Change cwd to dir if absolute path, else change cwd to concat of old cwd and dir
bool chdir (const char *dir);

// Split dir on the last /
// Verify that the filepath preceding the last part exists.
// Verify that the entire filepath does not exist
// Call filesys_create(<dir without last part>, DEFAULT_DIR_SIZE, 1)
bool mkdir (const char *dir);

// Invoke verify_filepath(file)
// Lookup dir_entry in cwd or in enclosing directory if file is absolute path
// Call filesys_open(dir_entry->inode, dir_entry->inode->isdir)
// Create a corresponding thread_fd_elem and add to thread->fd_map and return a fd int
static int open(const char * file);

// Look up fd map and get corresponding thread_fd_elem for fd. 
// Call file_close(thread_fd_elem->f) if thread_fd_elem->f not null
// Else call dir_close(thread_fd_elem->d)
static void close(int fd);

// Look up fd map and get corresponding thread_fd_elem for fd. 
//Verify that fd points to a directory by checking if the thread_fd_elem->d is not NULL by traversing fd_map.
// Return dir_readdir(thread_fd_elem->d, name)
bool readdir (int fd, char *name);

// Look up fd map and get corresponding thread_fd_elem for fd. 
//Return true if fd points to a directory by checking if the thread_fd_elem->d is not NULL, false otherwise
bool isdir (int fd);

// Look up fd map and get corresponding thread_fd_elem for fd. 
// If thread_fd_elem->f not null, return thread_fd_elem->f->inode->sector
// Else return thread_fd_elem->d->inode->sector
int inumber (int fd);
```


### Directory Content Structure

We will store (filename, inode sector number) mappings in a file (divided into sectors of course). This way, directories are treated as files. An example format of directory contents would follow something like (filename … inode_number … in_use\n):
  - “ foo1 … 12 … 1\nbar … 19 … 1\nfile3 … 89 … 0\n”...

This file structure can be easily marshalled into the `struct dir_entry`. `dir_create` handles properly formatting the contents into a file (with appropriate number of sectors)
To enumerate parent inode mapping (“..”) and handle for this dir’s inode (“.”) we add the following to the beginning of the file:
- “. … [this dir’s inode number] … 1\n .. … [parent dir’s inode number] … 1\n”

Additionally, the .. entry of root points to root's inode number.

### Algorithms

`verify_filepath(const char *fp)` takes in a filepath and checks if it exists in the current working directory or under root if preceded by a /. We will iteratively build a string tracking a parent directory of checked parts so far. We call get_next_part() on the file path and use dir_lookup(tracked parent dir, next part) to verify if the path exists in the traversal so far. Return false if any call to dir_lookup returns false. True otherwise.

Functions `filesys_create`, `filesys_open`, `filesys_remove` will search from root or cwd depending on if the passed in path is preceded with a slash.

Functions `filesys_create` and `filesys_open` include a `bool isdir` argument to convey to handle the target file object as a directory or normal file. Calls to `*_create` will reflect the inode type.

Syscalls `readdir`, `isdir`, and `inumber` require traversing the list fd_map of the thread. We traverse this list until we find the thread_fd_elem that corresponds to target fd.

### Synchronization

All of the synchronization provided in Task 1 for the cache will apply to any and all accesses to inodes in the syscalls. Accesses to `thread->fd_map` and `thread->cwd` are all safe because only the owning thread will access these members (except for during `exec` when the parent sets the `child_thread->cwd` to a copy of the parent's cwd, already serialized by scheduling the child after its members have been set.)

### Rationale

The thread_fd_t struct is modified so that it can accommodate either an open file or an open directory. This allows a thread's fd_map to contain all open files and directories rather than storying them in separate lists while retaining a very simple way of identifying an element of the list as belonging to either a directory or a regular file. By similar logic, adding the isdir argument to the aforementioned functions also serves as a quick way to change function behavior between accomodating a file and a directory. Finally, the `dir_entry` struct format lends itself to the directory contents structure enumerated above, as the fields can be marshalled to and from the directory file easily.

## Additional Question Section

### Read-ahead
Our cache only has temporal locality but it does not have spatial locality. Thus, to have spatial locality, when a data block is pulled from disk for a read or write operation, we would also pull the following data block for that file from disk. Read-ahead is only useful when done asynchronously, thus if a process attempts to access disk block 1, which is not in the cache, it should block until disk block 1 is read in, but once that read is complete, control should return to the process immediately. The read-ahead request for the disk block following disk block 1 in that file can be done asynchronously by creating a new process that reads this block into the cache. 


### Write-behind
For write-behind we need to periodically flush the dirty entries in the cache to disk. To do this, we can create a thread that periodically runs a function called `flush_cache` that iterates through the cache entries and writes to disk those that are dirty. Of course, this function would acquire the `buffer_cache_lock` for synchronization purposes before writing anything to disk. The last line of this function would call `timer_sleep (int64_t ticks)` with a specified tick number to make the thread sleep for TICKS timer ticks. Since Windows flushes its cache every 8 seconds, we decided that we would use the same policy for our cache. Thus, since there are 100 ticks per second, this would mean the thread would sleep for 800 ticks after calling `flush_cache` by calling `timer_sleep (800)`.










