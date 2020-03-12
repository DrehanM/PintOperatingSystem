Design Document for Project 2: Threads
======================================

## Group Members

* FirstName LastName <email@domain.example>
* FirstName LastName <email@domain.example>
* FirstName LastName <email@domain.example>
* FirstName LastName <email@domain.example>

# Task 1: Alarm Clock

`
/* threads/threads.h */
struct thread
{
	/* Everthing else is the same except we're adding the line below */
	int sleep_ticks; // keeps track of how long the thread needs to sleep for
}

struct list sleeping_threads; // keeps track of threads that are currently sleeping

`
