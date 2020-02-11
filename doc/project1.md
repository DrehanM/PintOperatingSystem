## Group Members

* Luke Dai <luke.dai@berkeley.edu>
* Christopher DeVore <chrisdevore@berkeley.edu>
* Dre Maharachi <dre@berkeley.edu>
* Benjamin Ulrich <udotneb@berkeley.edu>
* Diego Uribe <diego.uribe@berkeley.edu>

Project 1 Design Document
=======
# Task 1: Argument Passing

The first task can be broken down into two parts. 
Part A is separating the `char *file_name` into an array of individual words, and making letting this be char `*argv[]`. 
We also take the number of words in argv, and set this to be argc. 
Part B is putting argv and argc onto the processes stack so that when main is called it can access these variables. 

## Data Structures and Functions
### Part A

Data Structures

``` 
// Used to dynamically build words char by char. 

typedef struct building_word { 
	char c;  
	struct list_elem elem;  
} building_word_t;
```

```
// Used to dynamically build word by word

typedef struct word {
	char *word;
	int length_word;
	struct list_elem elem;
} word_t;
```

Functions

```
// mutates word_lst and adds all of the words from file_name to it
void get_word_list(char *file_name, list word_lst) 
```

```
// converts word_lst to argv, populates lengths with the length of each argv's word
// arguments: 
	word_lst: mutated from get_word_list
	argv: pointer to a list of length argc
	argv_lengths: empty list of length argc
void get_argv_from_list(list word_lst, char *argv[], int argv_lengths[]) 
```

### Part B
Data Structures

```
// Used by Part B

char *argv[];
int argc;
int argv_lengths[];
```

Functions
```
// pushes argc, argv, and a garbage return address to the stack. decrements if_esp
// returns 0 if reached bottom of memory
int load_arguments_to_stack(int argc, char *argv[], int argv_lengths[], void **if_esp)
```

```
// helper function containing asm instructions to push load to stack, decrements esp
// returns 0 if reached bottom of memory
int push_address_to_stack(uint address, void**if_esp)
int push_char_to_stack(char c, void**if_esp)
```

```
// call before anything is pushed to stack
// returns the number of bytes stack alignment needs to be, and add these bytes after 
int stack_alignment_calc(uint stack_pointer, int argc)
```

## Algorithms
Part A requires an algorithm to split up the char pointer we are given to a list of words. 
A simple algorithm that does this is by starting a new word if we encounter a space. 
In our previous homeworks we were given a maximum length that these words could be which allowed us to preallocate memory for the words. 
Since we don’t have this guarantee here, for example paths to files can be arbitrarily long, we can instead use the list data structure we used in homework one to dynamically build our words, using building_word_lst. 
Once a word has been completed, we can then fill in a correctly sized char array and make a word_t. 

In order to initialize the *argv[], we need to know how many words we have. 
To do this, we can once again use the list data structure, word_lst. 
We can dynamically add words to the list, and at the end look at how many words we have, and let this be argc. 
We use the function `get_word_list()` to convert the filename to a word list. 
Once `word_lst` is mutated, we can find its length using the built in `list_size()` function, and let this be `argc`. 
We then initialize `argv` to length `argc` and copy over the list to an array, which is done in function `get_argv_from_list()`.

Part B requires us to use `asm volatile` in order to push the arguments onto the stack. 
This is done in the function `load_arguments_to_stack()`, which uses helper function `push_address_to_stack()` and `push_char_to_stack()` that contains the actual `asm` instructions. 
We push each word in reverse order, starting from the null terminator character and ending at the first character. 
After we push the first character, we mark where on the stack each word begins in an internal list. 
Once we push all of the words, we calculate how much of a stack alignment we need, taking into account the addresses for `argv[i]`, `argv`, `argc`, and the return address that we will push after. 
This is calculation is done in `stack_alignment_calc()`.
We then push garbage data to align the stack, and then push on `argv[i]` addresses, `argv`, `argc`, and the garbage return adress. 

These functions will be called in `start_process()` in process.c.


## Synchronization
The `start_process()` function doesn't spawn any new threads, and doesn't access any resources shared across multiple threads.
In addition the functions we implement won't do this either, therefore we don't have to worry about synchronization in this part. 

## Rationale


### Part A
We decided to use a list to build the words instead of preallocating a buffer for the characters. 
Since words could potentially be really long (for example file paths), we didn't feel comfortable setting a limit on how big each word could be, and also didn't want to deal with the overhead of string copying if we had to expand the buffer. 
Another benefit is that the list data structure comes with a built in length method, which makes it easier to keep track of word length as opposed to using a seperate variable. 
Similar reasoning was used for the word list usage, where we didn't know how many words would be in file_name before hand. 

The reason we then convert everything back to an array is to reduce overhead in Part B by allowing easy indexing. 

Runtime: 
Converting `file_name` to list and from list to array both take linear time with respect to the number of characters in `file_name`. 


### Part B
We decided to make two helper functions `push_to_stack()` and `stack_alignment_calc()` to improve readability and testing. 
`push_address_to_stack()` and `push_char_to_stack()` will contain all of the `asm` instructions of actually pushing to the stack, which helps improve readability. 
`stack_alignment_calc()` is just a tedious calculation which deserves it's own function for testing purposes. 

Runtime:
Pushing to the stack also takes time linear with respect to number of characters, and number of words in `file_name`.


# Task 2: Process Control Syscalls

The task of implementing process control syscalls is also a two-part process. Syscalls are to be designed such that the kernel prevents invalid, corrupted, or null user-passed arguments from harming the OS, whether it be deliberately or accidentally. Thus, before processing the actual request, the kernel will carefully validate the arguments and memory passed in by the user program. If the syscall arguments are invalid, the kernel will move to terminate the calling user process (Part A). Afterwards, once validation has passed, the syscall handler will call the appropriate control sequence based on the arguments of passed to the interrupt frame. Since syscalls are enumerated based on the type of call, the handler will process the request in a switch-case sequence, identifying the syscall based on the contents of args[0] in syscall_handler. Once the syscall type has been identified, the appropriate control sequences will be deployed. We cover the control sequences for processing the following for syscalls in this section: HALT, PRACTICE, WAIT, EXEC (Part B).

## Part A: Argument Validation

### Data Structures & Functions

```
/* In threads/thread.h */

// Shared struct between parent and child that is created when parent calls wait on child.
// Used to communicate child process's completion status to waiting parent.
struct wait_status {
	// members to be used during WAIT syscalls
	struct semaphore dead;			//flag to signify if this thread has terminated.
	int exit_status;			//exit status of process upon termination.
	pid_t pid;				//process ID of the owner of this struct
	int reference_count;			//number of processes that point to this struct
	struct lock lock;			//lock to protect reference_count
	
	// members to be used during EXEC syscalls
	struct semaphore done_loading;
	int load_error;
	
	struct list_elem elem;			//underlying list element
}

// Process/Thread Control Block. Pintos is single-threaded, so processes and threads are the same thing.
struct thread {
	tid_t tid;  			    // Thread identifier.
	enum thread_status status;          // Thread state.
	char name[16];                      // Name (for debugging purposes).
	uint8_t *stack;                     // Saved stack pointer.
	int priority;                       // Priority.
	struct list_elem allelem;           // List element for all threads list.
	
	// Adding Process-related members
	pid_t pid;			    // Process identifier. Same value as TID.
	uint32_t *pagedir;		    // Page directory. Already existing in skeleton code.
	struct wait_status *wait_status;    // Shared between the parent and this thread to communicate during WAIT calls.
	struct list children;		    // List of wait_status objects shared by this thread and its children.
	
	struct list_elem elem; 
	unsigned magic;
}
	

// Create and allocate memory for a new wait_status struct for a new process
// Initialize the dead semaphore and reference_count lock
void init_wait_status(struct wait_status *ws, pid) {
	ws = malloc(sizeof(wait_status));
	ws->pid = pid;
	sema_init(ws->dead, 1);
	sema_init(ws->done_loading, 0);
	ws->load_error = 0;
	lock_init(ws->lock);
	lock_acquire(ws->lock);
	ws->reference_count = 2;
	lock_release(ws->lock);
}

//Free the memory taken up by the given wait_status struct and remove it from parent's children list.
void destroy_wait_status(struct wait_status *ws) {
	ASSERT(ws->dead != 0);
	list_remove(ws->elem);
	free(ws);
}

/* In threads/thread.c */

//Partially modifying thread_create
//Added lines to define pid and initialize wait_status struct.
tid_t thread_create(const char *name, int priority, thread_func *function, void *aux) {
	...
	tid = t->tid = allocate_tid();
	t->pid = (pid_t) tid;
	...
	init_wait_status(t->wait_status);
	list_push_front(thread_current()->children, t->wait_status->elem);
	...
	struct wait_status *child_ws = t->wait_status;
	thread_unblock();
	if (child_ws->load_error == 0) {
		// Parent waits for child to attempt finishing load (if it hasn't already)
		// Nothing happens if the child has already attempted to load since this semaphore will be 1 in that case
		sema_down(&(child_ws->done_loading)); 
	} else {
		tid = TID_ERROR;
	return tid;
}

/* In userprog/process.c */

//Given a list of wait_status structs, return the wait_status that has the given PID. NULL if no match.
struct wait_status *find_child_ws(list *children_ws, pid_t pid) {
	struct list_elem *e;
	struct wait_status *ws;
	  	for (e = list_begin(children_ws); e->next != NULL; e = e->next) {
	    		ws = list_entry(e, wait_status, elem);
	    		if (ws->pid == pid) {
	      			return ws;
	    		}
	 	}
	return NULL;  
}


//Fully modifying process_wait
int process_wait(pid_t child_pid) {
	struct thread *parent = thread_current();
	struct wait_status *child_ws = find_child_ws(parent->children, pid);
	if (child_ws == NULL) {return -1;}
	success = sema_try_down(&(child_ws->dead));
	if (!success) {return -1;}
	sema_down(&(child_ws->dead));
	int child_exit_status = child_ws->exit_status;
	destroy_wait_status(child_ws);
	return child_exit_status;

//Safely decrement the given wait status struct's reference_count. If reference_count reaches zero, destroy the wait_status.
void decrement_and_destroy_if_zero(struct wait_status *ws) {
	lock_acquire(&(ws->lock));
	ws->reference_count--;
	if (ws->reference_count == 0) {
		lock_release(&(ws->lock));
		destroy_wait_status(ws);
	} else {
		lock_release(&(ws->lock));
	}
	
//Decrement all reference_count variables shared by the current thread and cur's parent and cur's children.
//Sema up on the current threads wait_status->dead semaphore.
void decrement_all_references(struct wait_status *ws) {
	struct thread *cur = thread_current();
	decrement_and_destroy_if_zero(ws);
	struct wait_status *child_ws;
	for (e = list_begin(cur->children); e->next != NULL; e = e->next) {
	    	child_ws = list_entry(e, wait_status, elem);
	    	decrement_and_destroy_if_zero(child_ws);
	}
}

//Partially modifying process_exit
void process_exit(void) {
	... //rest of the existing function declaration. Remove the sema_up(&temporary) line.
	struct wait_status *ws = cur->wait_status;
	sema_up(&(ws->dead));
	decrement_all_references(ws);
	
//Partially modifying process_execute
// The semaphore temporary will ensure that child gets a chance to load its process.
// After the child has attempted a load, the semaphore will be up'd.
// Then, we examine the wait_status->dead->value. Since this is initalized to 1 before a wait is ever called,
	a thread_exit() would increment this once more, leading to a value of 2 if no parent ever waited.
tid_t process_execute(const char *file_name) {
	...
	tid = thread_create (file_name, PRI_DEFAULT, start_process, fn_copy);
  	if (tid == TID_ERROR) {
    		palloc_free_page (fn_copy);
    		return tid;
  	} 
  	struct wait_status *child_ws = find_child_ws(thread_current()->children, tid);
  	if (child_ws->load_error == 1) {
      		//load failed
      		return TID_ERROR;
    	} else {
		// load success
		return tid;
      	}
  }

//Partially modifying start_process
//If the load fails, we set the load_error to 1, up the done_loading semaphore, and exit the child process.
//If success, we up the done_loading semaphore only.
static void start_process(void *file_name_) {
	...
	struct wait_status *ws = thread_current()->wait_status;
	if (!success) {
		ws->load_error = 1;
		sema_up(&(ws->done_loading));
		thread_exit();
	} else {
		sema_up(&(ws->done_loading));
	}
	...	
	
/* In userprog/syscall.c */

static void syscall_handler(struct intr_frame *f) {
	uint32_t* args = ((uint32t *) f->esp);
	int validation_status_code = validate_syscall_args(args);
	int syscall_id = args[0];
	
	switch(validation_status_code) {
		Handle each validation_status_code according to the type of error.
			- 0: no error -> do nothing
			- 1: null reference -> kill?
			- 2: page violation -> invoke a page_fault?
			- 3: user virtual address space violation -> invoke a page_fault?
	
	switch(syscall_id) {
		case SYS_HALT: 
			shutdown_power_off();
		case SYS_EXIT:
			f->eax = args[1];
			print some stuff;
			thread_exit();
		case SYS_EXEC:
			pid_t pid;
			file_deny_write(
			pid = process_execute(args[1]);
			f->eax = pid;		
		case SYS_WAIT:
			pid_t pid = args[1];
			int child_exit_status = process_wait(args);
			f->eax = child_exit_status;
		case SYS_PRACTICE:
			f->eax = args[1] + 1;
	}
			
			
```
### Algorithms and Synchronization
Assuming we have validated syscall arguments correctly, we can process the various syscalls and their arguments appropriately. Return values will be stored in f->eax, as is convention.
#### PRACTICE: 
Set f->eax = args[1] + 1 and return. EZPZ.
#### HALT:
Call *shutdown_power_off()*, which shuts down the system if we are running QEMU or Bochs.
#### WAIT:
The WAIT syscall requires careful coordination of shared resources between the calling parent and the target child. As seen in the code above, we utilize the *wait_status* struct, which exists as a member of each thread. A oarent can access their children's wait_status structs by iterating through the list of wait_status structs that exist under the struct member *children*. When a process calls wait of a specific PID/TID, we iterate through the children to find the wait_status struct of the target child. If found, the current parent process then downs the semaphore *dead* in the child's wait_status and goes to sleep until the child willingly exists or is terminated by the kernel. If the target PID/TID is not found OR the parent has already called WAIT on the target child, the process_wait() call and, subsequently, the WAIT syscall returns with value -1. After the child has died and has appropriately set the dead semaphore up such that the parent can wake up. The dead child's exit status is then taken from the shared wait_status struct, ready to be returned by process_wait() and then the WAIT syscall. The same wait_status struct is then destroyed and removed from the children list. We also free all of the child's resources, including its thread and pagedir. Finally, we return the exit status. Some edge cases to watch out are:
- premature termination of the parent, before the child returns: in this situation, the parent will decrement the reference_count variables in each of its children's wait_status structs to signify the parent's death. Thus, when each of the children die, they will destroy their own wait_statuses on behalf of the dead parent when the reference_count reaches 0.
#### EXEC:
The EXEC syscall will ensure that the parent calls process_execute(args[1]), where args[1] is an executable's file name, and it will wait for the returned child pid. If pid == -1, then execution failed. We must ensure synchronization of this call between the parent and child. In particular, since we unblock the newly allocated child thread in *thread_create*, it is possible for the child thread to execute before the parent's call to *process_execute* returns. Thus, we have included two pieces of data (semaphore wait_status->done_loading and int wait_status->load_error) to handle the various context switch cases that can occur once the child thread is put on the ready queue. We initialize load_error to 0 and set it to 1 if and only if the child's call to *load* in *start_process* fails. We initialize semaphore wait_status->done_loading to 0, and only the parent thread can call down on this semaphore while the child can only call up on it. We enumerate the context switch situations below and verify correctness. We designate a pointer to the child's wait_status prior to *thread_unblock* being invoked:
- case 1: parent calls thread_unblock -> switch to child, child fails loading its process, sets load_error = 1, ups done_loading, child thread exits -> switch to parent, downs done_loading (but this does nothing since it was positive), returns from *thread_create* with child tid, check that load_error == 1, return TID_ERROR
- case 2: parent calls thread_unblock -> switch to child, child fails loading its process, -> switch to parent, parent downs done_loading and goes to sleep -> switch to child, sets load_error = 1, ups done_loading, exits -> parents wakes up, returns from *thread_create* with child tid, check that load_error = 1, return TID_ERROR
- case 3: parent calls thread_unblock -> switch to child, child succeeds loading, ups done_loading -> switch to parent ...
	- This case is also handled correctly since the parent will see eventually see that load_error == 1, and successfully return the child TID.
- case 4: parent calls thread_unblock, downs done_loading, and goes to sleep -> child process will finish its load and modify load_error as needed before the parent wakes up. 
	- The parent will eventually know if its child failed or succeeded since load_error is set correctly by the child.
In general, we have imposed the following ordering: Parent unblocks child, child attempts to load itself, parent checks status of child's load, parent returns child TID or TID_ERROR based on the result of the child's load.

TID is subsequently returned to the *syscall_handler* and cast to pid_t. Again, we store the returned PID or PID_ERROR in f->eax.

#### Minor modifications to EXIT and process_exit:
To accomodate the use of wait_status synchronization objects, a process will up its wait_status->dead semaphore and decrement all of its shared reference_count variables before exiting. This logic has been added to the end of *process_exit*.
		
### Synchronization

To recap the synchronization schemes used above, we use the dead semaphore in wait_status structs to signify the sleeping and waking of a parent process while it waits for the termiantion of its child. We implement a lock in tandem with the reference_count member of the wait_status struct to allow safe decrements to reference_count when either a parent or child dies, preventing any race conditions if the parent and child happen to be dying at the same time. Finally, we use the temporary semaphore to block the parent thread while the newly executed child can attempt to load its user program. This is to prevent exec from returning before the parent is informed of the child's death.

### Rationale

We opted to use a wait_status struct to impose synchronization between parent and child processes to prevent unnecessary read/write access between processes. To a parent process, the only pertinent information about its child is its state while the parent waits. This design follows a least privelege scheme. 

For the two more complicated syscalls in this task, EXEC and WAIT, the precise logic required to manipulate wait_status structs is the core to this design. Every single added or modified function and data structure maipulates wait_status structures in some form. We believe using a central data structure to drive cooperation between processes is far easier to implement and debug than having direct pointers between processes/threads.

We felt that writing out code in C (without implementing it) made thinking through edge cases and general logic easier. We elected to use short modular functions to ease debugging and allow additions of new features, if need be.

Finally, time/space complexity for each syscall are as follows:
- HALT & PRACTICE: 
	- Constant time. No argument dependence.
	- Constant space. No argument dependence.
- EXEC: 
	- Constant time as there are no calls with dependence on the number of children.
	- Linear space in terms of the size of the executing child program.
- WAIT:
	- Linear in time on the order of the number of children the calling process has (due to *find_child_ws* search loop)
	- Constant in time. There is no allocated data that depends on the parent or target child process.
		
# Task 3: File Operation Syscalls

  All file operation syscalls are handled by the syscall_handler function in syscall.c. Thus, to know if a user is calling a  file operation, we will check if args[0] is equal to any of the following: {SYS_CREATE, SYS_REMOVE, SYS_OPEN, SYS_FILESIZE, SYS_READ, SYS_WRITE, SYS_SEEK, SYS_TELL, SYS_CLOSE}. Once we identify which file operation a user is calling we will call the file_operation_handler function (define below) which handles synchronization and executes the desired file operation. This function will then use Pintos file system to perform the requested operation.
  To synchronize the file sytem operations we will use a global lock with. Before executing the file operation, we will call lock_acquire() on the lock inside the file_operation_handler. If succesful, we will execute the file operation. Otherwise, the thread will block until the lock has been released by the holder with lock_release(). This ensures that no file system functions are called concurrently.
  To ensure that the executable file of a running process is not modified we will make use of file_deny_write(). More specifically, immediately after the executable file is opened in the load function in process.c, we will call file_deny_write() on that file to disable write access. After the file is loaded, the load function calls file_close() which enables write access again on the file. Once this file is loaded into memory, it is impossible to write to it since it is in a restricted section of memory (Code). 
  
## Data Structures and Functions
 

Function:
```
static void file_operation_handler(__LIB_SYSCALL_NR_H file_operation) {}
```
This function synchronizes all the file_operations. It receives which file_operation it needs to perform from syscall_handler and calls the respective function. This function consists of a switch statement where each case corresponds to one of the file_operations. Before a file operation is called this function attempts to acquire the lock, if succesfull it executes the file operation, otherwise, it waits until the lock is released. After the file operation is completed, it releases the lock.

Global Lock:
This global lock will de defined in syscall.c as follows:
```
static struct lock global_lock;
lock_init(&global_lock)
```


### Hashmap
Add a hashmap to each thread, which maps `fd -> *file`. This hashmap will store the file descriptors of all the files a user has opened. Each file descriptor maps to a file pointer which can be used to call PINTOS file system operations in file.c. Each thread/process has its own hashmap of file descriptors.
use a dynamically reallocating hashmap @ https://github.com/robertkety/dataStructures/blob/master/hashMap.c

```
struct thread {
	tid_t tid;  			    // Thread identifier.
	enum thread_status status;          // Thread state.
	char name[16];                      // Name (for debugging purposes).
	uint8_t *stack;                     // Saved stack pointer.
	int priority;                       // Priority.
	struct list_elem allelem;           // List element for all threads list.
	
	// Adding Process-related members
	pid_t pid;			    // Process identifier. Same value as TID.
	uint32_t *pagedir;		    // Page directory. Already existing in skeleton code.
	struct wait_status *wait_status;    // Shared between the parent and this thread to communicate during WAIT calls.
	struct list children;		    // List of wait_status objects shared by this thread and its children.
	
	struct list_elem elem; 
	unsigned magic;

	hashmap fd_map; // file descriptor : *file map
}
```

Function:
```
*file get_file_from_fd(int fd){} 
```
gets `thread_current->fd_map` and gets the corresponding file pointer, or NULL if there isn't one

Below we describe the implementation of each of the file operations:
### Create
Function:
```
bool create(const char * file, unsiged initial_size){}
```
This function calls the filesys_create(const char * name, off_t initial_size) function in filesys.c. The parameters for filesys_create consist of the file name (* file) and file size (initial_size) provided by the user. create will return whatever filesys_create returns. 

### Remove
Function:
```
bool remove(const char * file) {}
```
This function calls the filesys_remove(const char * name) function in filesys.c. The parameter for filesys_remove is the name of the file (* file) provided by the user. remove will return whatever filesys_create returns. 


### Open
Function:
```
int open(const char * file) {}
```
This function calls the filesys_open(const char * name) function in filesys.c. The parameter for filesys_open is the name of the file (* file) provided by the user. If filesys_open returns a Null pointer, open will return -1. Otherwise, we return the file descriptor number. We also add the file descriptor and the corresponding pointer to `thread_current->fd_map`.
  
 
### Filesize
Function:
```
int filesize(int fd) {} 
```
Calls `get_file_from_fd(fd)` to get the `*file`, then uses this to call `file_length (struct file * file)` in file.c.  


### Read
Function:
```
int read(int fd, void * buffer, unsiged size) {}
```
Calls `get_file_from_fd(fd)` to get the `*file`. This function then calls file_read (struct file * file, void * buffer, off_t size) in file.c.

### Write
Function:
```
int write(int fd, void * buffer, unsiged size) {}
```
Calls `get_file_from_fd(fd)` to get the `*file`. This function then calls file_write (struct file * file, const void * buffer, off_t size) in file.c.

### Seek
Function:
```
void seek(int fd, unsiged position) {} 
```
Calls `get_file_from_fd(fd)` to get the `*file`. This function calls file_seek (struct file * file, off_t new_pos) in file.c. 


### Tell
Function:
```
unsiged tell(int fd) {}
```
Calls `get_file_from_fd(fd)` to get the `*file`. This function calls file_tell (struct file * file) in file.c.

### Close
Function: 
```
void close (int fd) {}
```
Calls `get_file_from_fd(fd)` to get the `*file`. This function calls file_close (struct file * file) in file.c, and removes fd from the threads hashmap.

## Algorithms



## Synchronization
As mentioned above, to prevent file system functions from being called concurrently we will use a global sempahore with an initial value of 1. The function `file_operation_handler` will be responsible for increasing (semaphore.up()) and decreasing (semaphore.down()) the value of the semaphore. Before a file operation is called, we will call semaphore.down() and when the operation terminates we will call semaphore.up().

## Rationale 
# Additional Questions
 
## 1) 
A test case that makes a syscall with an invalid stack pointer is sc-boundary-3. 
 
```
void
test_main (void)
{
	char *p = get_bad_boundary ();
	p--;
	*p = 100;

	/* Invoke the system call. */
	asm volatile ("movl %0, %%esp; int $0x30" : : "g" (p));
	fail ("should have killed process");
}
```

In this test the pointer p is set with `char *p = get_bad_boundary ();` to the highest address such that previous values are within valid memory in the bss segment, and p points to the first invalid location. `p--;` then decrements this pointer so that it points to the last byte within valid memory. With `asm volatile ("movl %0, %%esp; int $0x30" : : "g" (p));` a syscall  is attempted using p as the value for the stack pointer. Since this stack pointer is interpreted as a pointer to a four byte region, one byte is within valid memory but the final bytes are outside of it. As a result, this is an invalid pointer and the process is killed.

## 2)
sc-bad-arg is a test case that attempts a syscall with a valid stack pointer, however it fails because it tries to access values outside of valid memory. 

```
test_main (void)
{
  asm volatile ("movl $0xbffffffc, %%esp; movl %0, (%%esp); int $0x30"
                : : "i" (SYS_EXIT));
  fail ("should have called exit(-1)");
}
```

The instruction `movl $0xbffffffc, %%esp` directs the stack pointer to `0xbffffffc`, which is four bytes away from the base of user memory, and then sets its value to the call number for SYS_EXIT with `movl %0, (%%esp)`. `int $0x30` then performs the interrupt to kernel. On SYS_EXIT an argument is expected prior to the call number which would be at `0xc0000000` at the base of user memory. Since this location is outside of valid memory, the process is terminated. 

## 3)
Outside of the limits on the length of file names, there are no specified bounds on the size of the arguments being passed. It would be worth testing the behavior of the system in the case that there are either long arguments or very many arguments passed in a process which take up large amounts of space on the stack. In extreme cases, it is possible that the process would need to be allocated an extra page of stack space in order to handle these arguments, and the behavior of processes in these cases in which they require additional stack pages to run is also untested.
