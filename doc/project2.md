Design Document for Project 2: Threads
======================================

## Group Members

* Luke Dai <luke.dai@berkeley.edu>
* Christopher DeVore <chrisdevore@berkeley.edu>
* Dre Maharachi <dre@berkeley.edu>
* Benjamin Ulrich <udotneb@berkeley.edu>
* Diego Uribe <diego.uribe@berkeley.edu>

# Task 1: Efficient Alarm Clock
### Data Structures and Functions
```
/* threads/threads.h */
struct thread
{
	/* Everthing else is the same except we're adding the line below */
	int64_t wake_up_tick; // the tick when the thread should wake up
}

struct list sleeping_threads; // keeps track of threads that are currently sleeping
// this list is sorted from least to greatest by the sleep_ticks variable
```

### Algorithms
First, check whether the `timer_sleep()` is called with a positive number. If it is a negative, we return immediately. We then set `thread->wake_up_tick = timer_ticks() + ticks` to keep track of when the thread should wake up.
Finally, we add the thread to the `sleeping_threads` list ordered by the sleep_ticks from least to greatest.

In the `timer_interrupt()`, the time tick is incremented, and then we check through the `sleeping_threads` list for any threads that should wake up and schedule them.

### Synchronization
We disable the interrupts before inserting the thread into the `sleeping_threads` list and then block the thread. This ensures that the insertion is atomic and race conditions are avoided when multiple threads call `timer_sleep()` simultaneously.

### Rationale
We can use PintOS' list structure which comes with the feature of inserting an element into an ordered list. This can make implementing `sleeping_threads` easy. Disabling the interrupts is unavoidable to prevent racing so we limited it just the list insertion and thread blocking. Then `timer_interrupt` should still run pretty fast because we are only looking at an ordered list and stopping when the current time is less than the list element's `wake_up_tick`. This should at most be a linear complexity with respect to the number of threads.

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
    int priority;                       /* Current Priority. */
    int original_priority;		/* Priority we revert to when priority_donation_list is empty */
    struct list_elem allelem;           /* List element for all threads list. */
    
    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */
    
    /* Keeps track of the executable of this thread. */
    struct file *executable;

    struct lock fd_lock;

    struct list fd_map;
    
    /* 
    Keeps track of the priority donations made to this thread and the shared resource that caused the donation. 
    */
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
We will also define the following struct in thread.h. This struct will compose the `priority_donation_list` defined above.

```
typedef struct priority {
    int priority;
    void *resource_address;
    struct list_elem elem;
} priority_t;
```

We will create a function `resource_priority_comparator` which compares two `list_elem`s for `priority` and returns true if the first `list_elem` is of lower priority than the second. This will be used as an argument for the `list_max` function. 
```
bool resource_priority_comparator (const struct list_elem *a, const struct list_elem *b, void *aux)
```

Called when `lock_address` is released. The following function will update a thread's `priority_donation_list` and it's priority using `thread_set_priority()`. Calls `thread_yield()` if the priority is decremented. 
```
void decrement_priority(void *lock_address) {};
```

This function sets the priority of a thread to prioriy and adds a list element containing the `resource_address` and the `priority` value to the thread's `priority_donation_list`.
```
void donate_priority(thread_t *thread, void *resource_address, int prioriy) {};
```

### Algorithms
#### Lock Priority Donation

The function `decrement_priority` will iterate through the `priority_donation_list` to look up the priority that was donated by the shared resource that was just released. If the donated priority is lower than the current priority of the thread this function will not change the thread's priority. If the donated priority is the same as the current priority of the thread this function will set the thread's priority to the next highest priority donated. We can find the next highest priority donated by calling `list_max` on `priority_donation_list` using the `resource_priority_comparator`. This will take O(N) time where N is the number of elements in `priority_donation_list`. In both cases, the priority list element for this donated priority will be removed for the `priority_donation_list`. If the thread decreases its priority we will need to call `thread_yield()`. 

When a thread calls `lock_acquire` there are two possibilities: it succesfully acquires the lock or it goes to sleep. If the thread succesfully acquires the lock we will not change its priority. However, if it fails to acquire the lock we will donate our priority to the lock holder using the `donate_priority` function. Since our scheduler always runs the highest priority thread first, we know that the lock holder will always have an equal or lower priority than the thread attempting to acquire it so we just donate it directly. Similarly, when a thread releases a lock we will just call the `decrement_priority`.

#### Synchronized Shared Resource Preference

We need to modify the implementation of synchronization shared resources like semaphores, locks, and condition variables so that they give preference to higher priority threads. 

Semaphore:
In order to give preference to higher priority threads in semaphores we just need to find the highest priority thread in the list of waiting threads `waiters` everytime a thread calls `sema_up()`. We can find the highest priority thread in `waiters` by calling `list_max` on `priority_donation_list` using the `priority_comparator`. This takes O(N) time where N is the numner of elements in the `waiters`. This will ensure that when a thread holding the semaphore and releases it by calling `sema_up()`, the next thread that runs will be the one with the highest priority. Threads can get added in any order to the `waiters` list when `sema_down()` is called becuase `sema_up()` will find the highest priority thread everytime it is called.

Lock:
Since locks are implemented using a semaphore initialized to value one, if semaphores give preference to higher priority threads so will our lock. 

Condition Variable:
Similar to semaphore, condition variables release based off of a `waiters` list. The `waiters` list contains a list of semaphores, and each of the semaphores have one thread that is waiting on the semaphore to be upped. Thus in `cond_signal()`, we can find the highest priority thread in the `cond->waiters` list by calling `list_max`. We make these two function calls atomic by disabling syscalls. 

### Synchronization
For semaphore, we find the highest priority thread in the `waiters` inside the disabled interrupts section of sema_up(), thus it is atomic. This ensures that the priority of the threads do not change between sorting and unblocking the next thread. 

Similarly for condition variable, finding the highest priority thread in `waiters` is within a disabled interrupt section, ensuring that priorities aren't changed through an interrupt.

### Rationale
#### Lock priority:
We base our algorithm off of the fact that the thread that is currently being run is the thread with the highest priority, which is what we implemented in part 2A.
If the currently running thread tries to aquire a lock that is currently held, we can assume that the lock holder has an equal or lower priority than the thread currently being run. 
Thus, we donate our priority to this thread, and yield control of the cpu to that thread so that the blocking thread can run. 
Since a thread can be donated to multiple times, we need a way to identify what resource corresponds to which priority donation.
This is because we have to revert the priority back to it's original priority after we release the contested resource. 
Every thread therefore has a list that maps the priority to the shared resource it was donated for. 
If we release a resource that corresponds to the highest priority in our `priority_donation_list`, we then decrease our priority to the next highest priority in `priority_donation_list` and then yield control of the cpu so that the higher priority thread can run.
If `priority_donation_list` is empty, we instead revert `priority` to `original_priority`.

#### Synchronized Shared Resource Preference:
Currently threads that are waiting for the semaphore are unblocked in a FIFO fashion. 
Instead of this, since we want the highest priority threads to be unblocked first, we simply unblock the highest priority thread by calling the `list_max` function on the `waiters` list using an appropiate comparator.

Similar to what we described above, a lock is a sempahore with a value of one. Since semaphores are implemented to give control to higher priority threads, transitively so will locks. 

Lastly condition variables were similar to semaphores in that `cond_signal()` unblocked threads in a FIFO fashion.
By doing the same modification as in semaphores, we made sure to unblock the highest priority thread in the `waiters` by calling `list_max`. 

# Task 3: Scheduling Lab

# Additional Questions

### 1
from pg 11-12 of project spec:

"Member of struct thread: uint8_t *stack

Every thread has its own stack to keep track of its state. When the thread is running, the CPU’s
stack pointer register tracks the top of the stack and this member is unused. But when the CPU
switches to another thread, this member saves the thread’s stack pointer. No other members are needed to save the thread’s registers, because the other registers that must be saved are saved on
the stack."

### 2
When thread_exit is called, the thread's status is set to `THREAD_DYING`, and then the next thread is scheduled. In `thread_schedule_tail`, the next thread checks if the previous thread is dying, and if so frees the page. 
