Final Report for Project 3: File Systems
========================================

## Changes to the Design Document
For Task 1: Buffer Cache, the only change we made was including a size and offset argument to cache_read and cache_write to support more precise edits to sectors without the use of a bounce buffer.

For Task 2: Extensible Files, we chose to implement the inode_resize function as seen in discussion to better handle for rollbacks on failed sector allocation. Additionally, we encountered race conditions resultant of concurrent reads during file extension operations. To remedy this, we included a reader-writer monitor that allows the following accesses:
- concurrent reads and non-extending writes on different sectors
- concurrent reads on the same sector
- blocking extending writes

The monitor is designed to synchronize inode accesses. Since file reads will be observing the inode length, all reads need read access to the inode. Since non-extending writes also only read inode data, these are treated as readers as well. File extension is considered a writer operation since this requires overwriting the length member. Likewise, any access that requires a thread to edit inode data (like file_deny_write) is checked into the system as a writer.

Other changes we made were synchonizing the free map and caching it in memory (only writing to disk on shutdown). We also removed the inode_disk member of inode.

For Task 3: Subdirectories, the first change to our design included adding a separate verification function for directory operations. For example we created a `dir_create` function to be used instead of `inode_create` in `filesys_create`, which created an inode and initialized `.` and `..` to the directory. Another change we made to our design was deciding that we wouldn't allow deletion of the cwd. We also added a check to make sure we didn't read or write to a directory, and we added a  `dir_lock` in the underlying directory inode to make sure operations on directories were synchronized. 

We also found a couple cute bugs from project 1. The first was our argument validation turned out to be incorrect which lead to weird errors in the persistence calls. Another was that we commented out parts of `free-map.c` which resulted in free_map_allocate returning the same sector multiple times, overwriting files. 

## Student Testing Report
// Luke & Chris


## Project Reflection

## What went well
As we implemented the tasks in order, each person responsible for the majority of work in each task made sure to thoroughly explain or comment their code. This allowed for a much more seamless hierarchy of abstractions between tasks, preventing latent bugs from previous tasks entering the next tasks (i.e. we did not suffer from latent issues from Task 1 in Task 2 or Task 2 in Task 3). Thus, each task felt self-contained and much more manageable to implement.

## What could be improved
COVID19 rendered it increasingly difficult to synchronize schedules for working on the project together. Thus, we often found that work sessions consisted mostly of one or two members. Nevertheless, the modularity of the project ensured that this would not adversely affect our experience.


### Work by each Group Member:
Luke Dai: Student Testing Code & Testing Report.

Christopher DeVore: Helped with Task 3 Design, Student Testing Code & Testing Report.

Dre Mahaarachchi: Task 2 Implementation, Task 3 Design, helped with Task 3 Implementation.

Benjamin Ulrich: Task 1 Design, Task 1 Implementation, Task 2 Design, Task 3 Implementation.

Diego Uribe: Task 1 Design, Task 2 Design, helped with Task 2 Implementation, Task 3 Implementation, Additional Questions.

