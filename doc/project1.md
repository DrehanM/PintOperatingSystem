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
/* In userprog/syscall.c */

// Returns nonzero value if any of the following validation procedures fails. 0 otherwise.
int validate_syscall_args(uint32_t* args) {
	uint32_t error_code;
	error_code = in_userspace_and_notnull(args);
	if (error_code > 0) {return error_code;}
	error_code = is_valid_file_or_buffer(args);
	if (error_code > 0) {return error_code;}
	return 0;
}

// Returns 0 if all necessary arguments of the syscall are in valid userspace memory, are mapped in memory, and are not null pointers.
// Returns 1 if any required argument for the syscall is null.
// Returns 2 if any of the arguments are fully or partially in unmapped memory.
// Returns 3 if any arguments are fully or partially outside of user virtual address space.
int in_userspace_and_notnull(args);

// Returns 0 if syscall is not handling a file or buffer OR if the parameter files/buffers are in valid user space, mapped correctly, and not null
// Returns 1 if the file or buffer is a null pointer.
// Returns 2 if the file or buffer points to unmapped memory.
// Returns 3 if the file or buffer points to memory outside of user virtual address space.
int is_valid_file_or_buffer(args)
```

### Algorithms

Parameter validation will be implemented as a composite of two checks: verifying that the arguments are in mapped userspace and are not null and verifying if any pointers to buffers or files are addressed to mapped userspace and not null. We employ the use of two functions to do this, each returning some integer corresponding to success (0) or a specific error. These functions are then run in sequence. If either call returns nonzero, we immediately return this integer to syscall_handler.

*Syscall_handler* calls *validate_syscall_args(args)* passing in ((uint32_t*) f->esp) as a parameter, since the syscall args are to be stored at and above f->esp. 

Within *in_userspace_and_notnull*, all arguments are checked in 3 ways. First, we verify if the address of the argument exists in user virtual address space with is_user_vaddr(&args[i]). To ensure that the address is fully valid, we also verify that  is_user_vaddr(&args[i] + 3) is true. Second, we then check if the address exists in mapped user memory. To do this, we access the pagedir of the current thread via thread_current()->padedir and set this equal to uint32_t \*pd. Then, we call padedir_get_page(pd, &args[i]) and padedir_get_page(pd, &args[i] + 3), verifying that both of these evaluate to non-null. Finally, we access the contents of each argument verifying that args[i] != NULL. Since syscalls have varying numbers of arguments passed in, there exists a switch-case control statement to group syscall types by number of arguments passed in. This allows us to only check the addressing of first and last argument since these entries are likely to indicate if the arguments span invalid memory. All arguments are still checked for non-nullness.

In a similar fashion, we employ a switch-case statement in *is_valid_file_or_buffer*, grouping cases by whether the specific syscall contains a pointer to a file, a pointer to a buffer, or neither. File pointers are stored in args[1] so we carefully check if this pointer is pointing to mapped userspace and is not null using sequential calls is_user_vaddr and pagedir_get_page on args[1] (not &args[1]). We then verify non-nullness with *args[1] != NULL. Buffer pointers are stored in args[2], so for these syscalls the same checks for file pointers mentioned before are employed on args[2] and *args[2].

When *validate_syscall_args* finally returns with an integer, *syscall_handler* will elect to kill the offending process appropriate if a nonzero value is returned. Specifically, if 3 or 1 is returned, we elect to use *kill* from exception.c to terminate the process. If 2 is returned, we elect to use *page_fault* from exception.c to denote a violation on unmapped page memory. Termination of a process also involves appropriately freeing shared resources between the process's parent or its children, if any. We discuss the synchronization protocols for termination in more detail below.  If 0 is returned, we move to process the syscall normally, as outlined in Part B below.

### Synchronization

No synchronization is utilized during argument validation as this is done in serial and independently of other processes.

### Rationale

The above design seems the most logically sounds as it checks for all of the possible invalid memory states that could corrupt a kernel thread during a system call. We have decoupled general argument checks and file/buffer memory checks in order to have an easier time debugging if such validations fail. The code required for implementing the validation schemes will mostly involve switch case statements, which are straightforward and easy to debug. Time complexity for a check will be at most on the order of the number of arguments passed in, since each check (null check, page check, and address space check) are all constant time checks. Even file/buffer memory checks are constant as they employ the same logic. Thus, since we can have at most 3 arguments in a syscall, argument validation operates in constant time. No extra memory is utilized as the checks are done on a pointer to the arguments themselves, which is already declared in the skeleton code for *syscall_handler*.

## Part B: Syscall Routine

### Data Structures & Functions

```
/* In threads/thread.h */

// Shared struct between parent and child that is created when parent calls wait on child.
// Used to communicate child process's completion status to waiting parent.
struct wait_status {
	struct semaphore dead;			//flag to signify if this thread has terminated.
	int exit_status;			//exit status of process upon termination.
	pid_t pid;				//process ID of the owner of this struct
	int reference_count;			//number of processes that point to this struct
	struct lock lock;			//lock to protect reference_count
	
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
	

//Create and allocate memory for a new wait_status struct for a new process
// Initialize the dead semaphore and reference_count lock
void init_wait_status(struct wait_status *ws, pid) {
	ws = malloc(sizeof(wait_status));
	ws->pid = pid;
	sema_init(ws->dead, 1)
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
  	sema_down(&temporary);
  	struct wait_status *child_ws = find_child_ws(thread_current()->children, tid);
  	switch (child_ws->dead->value) {
    		case 2:
      			//load failed
			destroy_wait_status(child_ws);
      			return TID_ERROR;
    		case 1:
      			// load success
      			return tid;
    		default:
      			//something else happened
			// PANIC
			destroy_wait_status(child_ws);
			return tid;
  }

//Partially modifying start_process
//If the load fails, we up the semaphore and exit the child process.
//If success, we up the semaphore.
static void start_process(void *file_name_) {
	...
	if (!success) {
		sema_up(&temporary);
		thread_exit();
	}
	sema_up(&temporary); 
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
The EXEC syscall will ensure that the parent calls process_execute(args[1]) and it will wait for the returned child pid. If pid == -1, then execution failed. We must ensure synchronization of this call between the parent and child. In particular, since *load*, called in *start_process* is called by the child thread after it has been unblocked, it is essential that the parent waits until the child has attempted to load. We down a semaphore as the parent after the child thread has been created but before the child process has loaded its user program. This way, the child thread can attempt to load the user program. If successful, the child process will return from *start_process* normally, uping the semaphore that the parent down'd. If the child process fails to load, then the semaphore is up'd before the child calls *thread_exit*. After the child process has attempted to load and the parent wakes up, the parent can check the child's wait_status semaphore value. This value is initalized to 1, before a WAIT is ever invoked. However, during thread_exit, this value is incremented by 1. This means that the wait_status semaphore value can either be a 1 (if the child is exiting after the parent calls WAIT) or a 2 (when the child fails to load and exits before wait is ever called). If the value of the child's wait_status semaphore is 2 when the parent wakes up after an exec call, then the parent knows the child has died. Thus, we return -1 from the EXEC syscall. Otherwise, we return the value retured from *process_execute*, which is either the child's PID if it survives and loads properly, or -1 (PID_ERROR) if the child cannot properly allocate memory. Again, we store the returned PID or PID_ERROR in f->eax.
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
    To synchronize the file sytem operations we will use a global lock or semaphore with an initial value of 1. Before executing the file operation, we will call semaphore.down() inside the file_operation_handler to attempt to decrement the integer. If succesful, we will execute the file operation. Otherwise, the thread will block until the value is positive, and then unblock and decrememt the value. This ensures that no file system functions are called concurrently.
  
 ## Data Structures and Functions
 
 ```
 This function synchronizes all the file_operations. It receives which file_operation it needs to perform from syscall_handler.
 static void file_operation_handler(__LIB_SYSCALL_NR_H file_operation) {}
 ```
 Below we describe how the file_operation_handler will perform each of the following file operations:
 ### Create
 
 ### Remove
 
 ### Open
 
 ### Filesize
 
 ### Read
 
 ### Write
 
 ### Seek
 
 ### Tell
 
 ### Close
 
 ## Algorithms
 
 ## Synchronization
 As mentioned above, to prevent file system functions from being called concurrently we will use a global sempahore with an initial value of 1. The function file_operation_handler will be responsible for increasing (semaphore.up()) and decreasing (semaphore.down()) the value of the semaphore. Before a file operation is called, we will call semaphore.down() and when the operation terminates we will call semaphore.up().
 
 ## Rationale 
  

 
