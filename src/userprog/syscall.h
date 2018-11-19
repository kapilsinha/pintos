#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"

int valid_pointer(void *vaddr);
void exit_with_status(int status);
struct file *fd_to_file(struct thread *t, int fd);
struct file_descriptor *fd_to_file_desc(struct thread *t, int fd);
void close_files(struct thread *t);

void syscall_init(void);
void sys_halt(void);
void sys_exit(int status);
int sys_exec(const char *file_name);
int sys_wait(tid_t pid);
bool sys_create(const char *file_name, unsigned initial_size);
bool sys_remove(const char *file_name);
int sys_open(const char *file_name);
int sys_filesize(int fd);
int sys_read(int fd, const void *buffer, unsigned size);
int sys_write(int fd, const void *buffer, unsigned size);
void sys_seek(int fd, unsigned position);
unsigned sys_tell(int fd);
void sys_close(int fd);
void sys_nosys(void);

#endif /* userprog/syscall.h */
