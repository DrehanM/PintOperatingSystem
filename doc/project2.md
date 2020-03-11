Design Document for Project 2: Threads
======================================

## Group Members

* Luke Dai <luke.dai@berkeley.edu>
* Christopher DeVore <chrisdevore@berkeley.edu>
* Dre Maharachi <dre@berkeley.edu>
* Benjamin Ulrich <udotneb@berkeley.edu>
* Diego Uribe <diego.uribe@berkeley.edu>

# Task 1: Efficient Alarm Clock

# Task 2: Priority Scheduler
Task 2 consists of implementing the priority scheduler and priority donation. Thus, we decided to divide into two sections: section 2a for the priority scheduler and section 2b for priority donation.

## Section 2a: Priority Scheduler

### Data Structures and Functions
We will create a function `priority_comparator` which compares two `list_elem`s for threads and returns true if the first thread is of lower priority than the second. This will be used as an argument for the `list_max` function. 
```
bool priority_comparator (const struct list_elem *a, const struct list_elem *b, void *aux)
```
### Algorithms
The majority of this aspect of priority scheduling is already implemented in the `list` library file and in `schedule` within `thread.c`. We will change `list_pop_front` to `list_max` in the `next_thread_to_run` function of `thread.c` so that when a new thread is chosen from the ready queue it chooses the one with highest priority. 

### Synchronization
Synchronization is already handled by the thread scheduler, so this is not of concern.

### Rationale
Priority is incorporated into the list of processes in THREAD_READY state by popping items off `ready_list` in order of priority, rather than in FIFO order. This is done using the function `list_max` for the included list struct, which takes as a parameter a function `priority_comparator` to compare the priority of threads. Although we could alternatively use `list_insert_ordered` to maintain a list that is sorted by priority from which we can easily pop off the highest priority thread, the fact that we are also implementing priority donation means that threads within the list may change priority, requiring the list to be sorted again. Both functions `list_max` and `list_insert_ordered` are O(N) time complexity with N being the length of the list, so `list_max` is used to avoid the need for additional sorting.

When a thread is created or calls `thread_yield`, it is first placed in `ready_list`, so control will return to that thread if it has the highest priority. Otherwise the highest priority thread is scheduled as desired.


## Section 2b: Priority Donation

### Data Structures and Functions
We will modify the thread structure in thread.h to include a priority donation pintos list. Each element of this list will contain the priority being donated and the address of the shared resource (lock, semaphore, monitor, etc.) that triggered this priority donation.
```
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */
    
    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */
    
    /* Keeps track of the executable of this thread. */
    struct file *executable;

    struct lock fd_lock;

    struct list fd_map;
    
    /* Keeps track of the priority donations made to this thread and the shared resource that caused the donation. */
    struct list priority_donation_list;

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif

    struct wait_status *wait_status;    // Shared between the parent and this thread to communicate during WAIT calls.
	  struct list children;		            // List of wait_status objects shared by this thread and its children.

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };
```
We will also define the following struct in thread.h. This struct will compose the priority donation list defined above.

```
typedef struct priority {
    int priority;
    void *resource_address;
    struct list_elem elem;
} priority_t;
```

When a thread yields ownership of a shared resource (semaphore, lock, monitor) it might need to modify its priority. The following function will update a thread's priority and its priority_donation_list.
```
void decrement_priority(void *resource_address) {};
```

This function sets the priority of a thread to prioriy and adds a list element containing the resource_address and the priority value to the threads priority_donation_list.
```
void donate_priority(void *resource_address, int prioriy) {};
```
### Algorithms

The function `decrement_priority` will iterate through the priority_donation_list to look up the priority that was donated by the shared resource that was just released. If the donated priority is the same as the current priority of the thread this function will set the threads priority to the next highest priority donated. If the donated priority is lower than the current priority of the thread this function will not change the threads priority. In both cases, the priority list element for this donated priority will be removed for the priority_donation_list. If the thread decreases its priority we will need to call thread_yield().

Locks:
When a thread calls lock_acquire there are two possibilities: it succesfully acquires the lock or it goes to sleep. If the thread succesfully acquires the lock we will not change its priority. However, if it fails to acquire the lock we will donate our priority to the lock holder using the `donate_priority` function. Since our scheduler always runs the highest priority thread first, we know that the lock holder will always have a lower priority than the thread attempting to acquire it so we just donate it directly. Similarly, when a thread releases a lock we will just call the decrement_priority.

Sempahores:


Monitors:


### Synchronization


### Rationale


# Task 3: Scheduling Lab

# Additional Questions

### 1

from pg 11-12 of project spec:

Member of struct thread: uint8_t *stack

Every thread has its own stack to keep track of its state. When the thread is running, the CPU’s
stack pointer register tracks the top of the stack and this member is unused. But when the CPU
switches to another thread, this member saves the thread’s stack pointer. No other members are needed to save the thread’s registers, because the other registers that must be saved are saved on
the stack.
