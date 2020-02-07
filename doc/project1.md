## Group Members

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
###Part A

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

###Part B
Data Structures

```
// Used by Part B

char *argv[];
int argc;
int argv_lengths[];
```

Functions
```
// pushes argc, argv, and a return address to the stack. decrements esp
// returns 0 if reached bottom of memory
int load_arguments_to_stack(int argc, char *argv[], int argv_lengths[], uint return_address, void **if_esp)
```

```
// helper function containing asm instructions to push load to stack, decrements esp
// returns 0 if reached bottom of memory
int push_to_stack(uint load, void**if_esp)
```

```
// returns the number of bytes the stack alignment needs to be
int stack_alignment_calc(uint stack_pointer, int argc)
```

##Algorithms
Part A requires an algorithm to split up the char pointer we are given to a list of words. 
A simple algorithm that does this is by starting a new word if we encounter a space. 
In our previous homeworks we were given a maximum length that these words could be which allowed us to preallocate memory for the words. 
Since we don’t have this guarantee here, since for example paths can be arbitrarily long, we can instead use the list data structure we used in homework one to dynamically build our words, using building_word_lst. 
Once a word has been completed, we can then fill in a correctly sized char array and make a word_t. 

In order to initialize the *argv[], we need to know how many words we have. 
To do this, we can once again use the list data structure, word_lst. 
We can dynamically add words to the list, and at the end look at how many words we have, and let this be argc. 
We use the function get_word_list to convert the filename to a word list. 
Once we have argc, we can initialize argv to length argc and copy over the list to an array, which is done in function get_argv_from_list.

Part B requires us to use asm volatile in order to push the arguments onto the stack. 
This is done in the function load_arguments_to_stack. 
We push each word in reverse order, starting from the null terminator character and ending at the first character. 
After we push the first character, we mark where on the stack each word begins in an internal list. 
Once we push all of the words, we calculate how much of a stack alignment we need, taking into account the addresses for argv[i], argv, argc, and return address that we will push after. 
This is done in stack_alignment_calc.
We then push garbage data to align the stack, and then push on argv[i] addresses, argv, argc, and the return address. 


##Synchronization
	

##Rationale



Task 2: Process Control Syscalls

Task 3: File Operation Syscalls

  All file operation syscalls are handled by the syscall_handler function in syscall.c. Thus, to know if a user is calling a  file operation, we will check if args[0] is equal to any of the following: {SYS_CREATE, SYS_REMOVE, SYS_OPEN, SYS_FILESIZE, SYS_READ, SYS_WRITE, SYS_SEEK, SYS_TELL, SYS_CLOSE}. Once we identify which file operation a user is calling we will call the file_operation_handler function (define below) which handles synchronization and validates the user's arguments. If the arguments are valid, file_operation_handler will then use Pintos file system to perform the requested task.
  
  
  
  To synchronize the file system operations system we will use a global lock. 

 
