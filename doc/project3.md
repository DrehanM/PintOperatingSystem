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
```

```c
struct cached_sector {
	block_sector_t sector_idx; // check against when we go through the cache
	block_sector_t inode_idx; // where is the corresponding inode located in memory? If this sector is an inode, then it’s exactly the same as sector_idx
	list_elm elem; 
	bool dirty; 
	struct lock sector_lock; // acquire if currently reading/writing to this sector
	[]byte data; // of size BLOCK_SECTOR_SIZE, could be inode or data block, up to caller to cast to correct one
}
```


### Algorithms


### Synchronization


### Rationale


