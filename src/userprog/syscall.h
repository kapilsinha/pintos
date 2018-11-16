#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"

int valid_pointer(void *vaddr);
void exit_with_status(int status);

void syscall_init(void);
void sys_halt(void);
void sys_exit(int status);
int sys_exec(const char *file_name);
int sys_wait(tid_t pid);
int sys_write(int fd, const void *buffer, unsigned size);
void sys_nosys(void);

#endif /* userprog/syscall.h */
