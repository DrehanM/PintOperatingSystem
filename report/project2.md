Final Report for Project 2: Threads
===================================
# Task 1: Scheduling Lab Write-Up

Chris, Luke, and Dre! 

# Task 2: Final Report Project 2

## Changes to the Design Document
For task 1, the efficient alarm clock, we did not make any changes to the original design doc. The only function that was not mentioned explicity in the initial design document was the `sort_ticks` comparator which was used to keep the list of sleeping threads sorted by the time at which each thread needs to wake up.

Task 2a, the priority scheduler, was mostly correct except for the fact that we forgot to preempt the parent thread by calling `thread_yield` in `thread_create` if the child thread has a higher priority. Other than that, we did not have to make any other changes. 

For task2b, priority donation, we had to make several changes to our initial design document. First, our initial design did not account for nested donations. To fix this, we added two additional elements to the thread struct: 
`
struct thread *blocking_thread;
void *blocking_resource;
`
With this change, when a thread receives a donation it will first update its `priority_donation_list` and it will then check if there is a `blocking_thread`. If `blocking_thread` is `NULL`, then the thread does not need to pass along the donation; however, if there is a `blocking_thread` this thread will donate the received donation to the `blocking_thread` on the `blocking_resource`. This process will be repeated by calling the `priority_donation` function recursively until the `blocking_thread` field of a thread that is receiving the donation is NULL. Thus, this allows chain donations to occur. The reason we need to keep track of the `blocking_resource`, in addition to the `blocking_thread`, is becuase donations are made for a specific resource. This way, when the thread that receives the donation releases the `blocking_resource` we have a way of reverting back its priority. 

Similarly, we forgot to disable interrupts in `lock_acquire`, `lock_release`, and when calling `thread_set_priority` in the original design documents. Evidently, these changes were implemented. 

Another modification that we made to the thread struct, was adding the `bool donated` field. This essentially allowed us to easily check if a thread currently has a donation. This was used in the `thread_set_priority` to make sure that a thread cannot lower its priority maliciously if it has received a donation. 

## Project Reflection

## What went well
The work was evenly divided by the group members and all the group members were always in the same page. We worked on the design document all together, thus, from the start we had clarity in our implementation approach. Also, the thoroughness and clarity of our design document, except for the nested donation bug, facilitated the implementation process. We did not have any major bugs during the implementation cylce. 

## What could be improved
Although we worked together on the design document, it was difficult to work on the implementation as a group mostly because of the Covid-19 situation. All the group members were working remotely from home; thus, it was harder to collaborate all together. The result was that group members worked on the coding of only a section of the design document. We would appreciate some advice as to what is the best way to "peer-code" for project 3. 

### Work by each Group Member:
The project work was evenly divided between the group members. 

Luke Dai worked on the design and implementation of the efficient alarm clock and on the Scheduling Lab.

Christopher DeVore worked on the design and implementation of task 2a, the priority scheduler. Similarly, he worked on the Scheduling Lab and also contributed to the additional question section of the design document. 

Dre Mahaarachchi worked on the additional question section of the design document and on the scheduling lab. He also worked on the implementation of task 2b, priority donation. Most of his work for task2b was debugging test cases. 

Benjamin Ulrich worked on the design and implementation of task2b, priority donation.

Diego Uribe worked on the design and implementation of task2b, priority donation. He also worked on writting the final project report. 
