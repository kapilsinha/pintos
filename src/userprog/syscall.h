#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

int valid_pointer(void *vaddr);
uint32_t pop_stack(struct intr_frame *f);
void exit_with_status(int status);

void syscall_init(void);
void sys_halt(void);
void sys_exit(int status);
int sys_write(int fd, const void *buffer, unsigned size);
void sys_nosys(void);
void syscall_handler(struct intr_frame *);

#endif /* userprog/syscall.h */
