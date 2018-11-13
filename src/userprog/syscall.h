#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init(void);

void sys_halt(void);
int sys_write(int fd, const void *buffer, unsigned size);
void sys_nosys(void);

#endif /* userprog/syscall.h */
