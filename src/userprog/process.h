#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct start_process_param {
  char *filename;
  struct child_thread_coord *child_thread_coord; 
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

bool install_page (void *, void *, bool);

#endif /* userprog/process.h */
