#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"
#include "threads/interrupt.h"

typedef int mapid_t;

int valid_pointer(void *vaddr, struct intr_frame *f);
int valid_pointer_range(void *vaddr, unsigned size, struct intr_frame *f);
struct mmap_table_entry *find_mmap_entry(mapid_t mapping, struct thread *t);
void exit_with_status(int status);
struct file *fd_to_file(struct thread *t, int fd);
struct file_descriptor *fd_to_file_desc(struct thread *t, int fd);
void close_files(struct thread *t);

void syscall_init(void);
void sys_halt(struct intr_frame *f);
void sys_exit(int status, struct intr_frame *f);
int sys_exec(const char *file_name, struct intr_frame *f);
int sys_wait(tid_t pid, struct intr_frame *f);
bool sys_create(const char *file_name, unsigned initial_size, struct intr_frame *f);
bool sys_remove(const char *file_name, struct intr_frame *f);
int sys_open(const char *file_name, struct intr_frame *f);
int sys_filesize(int fd, struct intr_frame *f);
int sys_read(int fd, const void *buffer, unsigned size, struct intr_frame *f);
int sys_write(int fd, const void *buffer, unsigned size, struct intr_frame *f);
void sys_seek(int fd, unsigned position, struct intr_frame *f);
unsigned sys_tell(int fd, struct intr_frame *f);
void sys_close(int fd, struct intr_frame *f);
mapid_t sys_mmap(int fd, void *addr, struct intr_frame *f);
void sys_munmap(mapid_t mapping, struct intr_frame *f);
bool sys_chdir(const char *dir, struct intr_frame *f);
bool sys_mkdir(const char *dir, struct intr_frame *f);
bool sys_readdir(int fd, char *name, struct intr_frame *f);
bool sys_isdir(int fd, struct intr_frame *f);
int sys_inumber(int fd, struct intr_frame *f);
void sys_nosys(struct intr_frame *f);

#endif /* userprog/syscall.h */
