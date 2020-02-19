#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);

void get_word_list(char *file_name, struct list *word_lst);
void get_argv_from_list(struct list *word_lst, char *argv[], int argv_lengths[]);

int stack_alignment_calc(void* stack_pointer, int argc);
int load_arguments_to_stack(int argc, char *argv[], int argv_lengths[], void **if_esp);

void process_exit (void);
void process_activate (void);


#endif /* userprog/process.h */
