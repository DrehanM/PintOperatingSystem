Final Report for Project 1: User Programs
=========================================

During our discussion with the TA about our design document, the TA brought up a few changes that we implemented. First, we did not use hashmaps to store our file descriptors; we used the built-in linked list struct instead. Second, we heard that we did not need to worry about `STDIN` and `STDOUT` cases in the autograder, so we disregarded it. Unfortunately, the second change was erroneous as we later found out that not handling `STDOUT` in the write function caused kernel panics. Once we added it, we could finally start getting some basic tests passing.

Most of the time, we had no major trouble implementing what we wrote in the design document because of our thoroughness in the planning phase. Unfortunately, one thing that plagued us was the lack of details on the argument validation on the file operation syscalls. We added it after writing everything else which caused some parts of the code to break and errors to mix with each other when testing. This addition was the problem for most of the debugging phase as we did not have as clear of a plan as the rest of the project. This demonstrates to us the importance of the design document in guiding our coding process in this project.

Initially, we planned to do all of the argument validation within the function `validate_syscall_args` which would return zero for valid arguments, and otherwise return larger values depending on whether it was a case of null reference, page violation, or user virtual address space violation. It was structured as a switch statement to handle the varying types of arguments of the syscalls. In the case of invalid arguments, we initially passed `-1` into the `eax` register to return and then exited the syscall handler. However, this failed because it did not cause threads with invalid syscalls to exit. This design was almost entirely changed in order to correctly complete this aspect of the project. First, rather than having a complex series of switch statements to validate the arguments for different syscalls, the function in charge of validating these arguments was simplified to accept as a parameter the number of arguments for a given syscall, and then ensure that these arguments were within user virtual address space and that pointers were within valid memory. An additional function was added to ensure that files were not null. Second, upon detecting invalid arguments we set the thread’s exit status to `-1` and call exit to deshedule the thread and remove it.

The last minor problem that propped up was not handling the exit from file operation syscalls correctly. We had some memory leaks and forgot to unlock the global file lock when a thread exited due to invalid arguments. We spent some time identifying the problem and patched it in a timely manner.

In conclusion, with only one major issue in the argument validation field, Project 1 was a really successful project without too many hiccups. There were definitely members of the group that put more work into this project. With the insertion of a fifth member into the group, the work allocation was a bit uneven due to the difficulty of everyone meeting up at the same time. In the future, we could most likely see a more balanced effort from everyone.


## What everyone worked on:

Luke Dai -  File Operation Syscalls

Christopher DeVore - File Operation Syscalls, Argument Passing

Dre Maharachi - Argument Validation

Benjamin Ulrich - Argument Passing, File Operation Syscall, Argument Validation

Diego Uribe - File Operation Syscall, Argument Passing. Argument Validation

