#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <debug.h>
#include "threads/synch.h"

/* File-system operations share this lock until the file system has
   its own synchronization. */
extern struct lock filesys_lock;

void syscall_init (void);
void syscall_close_all (void);
void syscall_exit_process (int status) NO_RETURN;

#endif /**< userprog/syscall.h */
