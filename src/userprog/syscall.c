#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "pagedir.h"
#include "devices/shutdown.h"

static void syscall_handler(struct intr_frame *);

int valid_pointer(void *ptr);
int sys_write(int fd, const void *buffer, unsigned size);
uint32_t pop_stack(struct intr_frame *f);

/* TODO: Check address pointers for the following syscalls:
 * exec(char *file) => First argument is char pointer
 * create(char *file, unsigned size) => First argument is char pointer
 * remove(char *file) => First argument is char pointer
 * open(char *file) => First argument is char pointer
 * read(int fd, void *buffer, unsigned size) => Second argument is pointer
 * write(int fd, void *buffer, unsigned size) => Second argument is pointer
 * mmap(int fd, void *addr) => Second argument is pointer
 * chdir(char *dir) => First argument is pointer
 * mkdir(char *dir) => First argument is pointer
 * readdir(int fd, char name[]) => Second argument is array
 */

/* Check if the pointer is a user virtual address and that the virtual address
    is mapped to physical memory. */
int valid_pointer(void *ptr) {
    if (is_user_vaddr(ptr) && pagedir_get_page(thread_current()->pagedir, ptr)){
        return 1;
    }
    return 0;
}

/* Pops an element from the interrupt stack frame passed in. */
uint32_t pop_stack(struct intr_frame *f) {
    uint32_t elem;
    elem = *((uint32_t *) f->esp++);
    return elem;
}

/*
 * TODO: Come up with a cleaner, more extensible way of mapping file
 * descriptors to `file' struct pointers.
 */
/* Converts the file descriptor to the corresponding file struct. We can
    simply cast the int to a struct because this is how we defined it. */
// struct file *fd_to_file(int fd) {
//     struct file *file;
//     file = (struct file *) fd;
//     if ((void *) file >= PHYS_BASE) {
//         // TODO: This needs to be checked
//         thread_exit();
//         return NULL;
//     }
//     return file;
// }

void syscall_init(void) {
    intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Terminates Pintos by calling shutdown_power_off()
    (declared in devices/shutdown.h). This should be seldom used, because you
    lose some information about possible deadlock situations, etc. */
void sys_halt(void) {
    shutdown_power_off();
    NOT_REACHED();
}

/* System call for writing to a file descriptor. Writes directly to the console
    if fd is set to 1. Returns the number of bytes written. */
int sys_write(int fd, const void *buffer, unsigned size) {
    // Check that the buffer is a valid pointer and if not return
    if (!valid_pointer((void*) buffer)) {
        // TODO: Not sure if this should exit or return 0 or both
        thread_exit();
        return 0;
    }
    // If fd is 1, we need to write to the console
    if (fd == 1) {
        // TODO: If size is greater than a couple hundred bytes, break it up
        putbuf(buffer, size);
        return size;
    }
    // Otherwise, write to the file
    else {
        // TODO: Change to actually write to a file instead of the console
        putbuf(buffer, size);
        return size;
    }

}

/* Called for system calls that are not implemented. */
void sys_nosys(void) {
    printf("ENOSYS: Function not implemented.\n");
}

static void syscall_handler(struct intr_frame *f) {
    printf("System call!\n");
    int fd;
    const void *buffer;
    unsigned int size;
    // Pop the syscall number from the interrupt stack frame
    uint32_t call_num = f->esp;
    switch (call_num) {
        // Terminate pintos by calling shutdown_power_off() in shutdown.h
        case SYS_HALT:
            printf("Halt syscall!\n");
            sys_halt();
            NOT_REACHED();

        case SYS_WRITE:
            printf("Write syscall!\n");
            // Arguments should be pushed in reverse order
            fd = (int) pop_stack(f);
            buffer = (void *) pop_stack(f);
            size = (unsigned int) pop_stack(f);
            // Write to the file or console as determined by fd
            sys_write(fd, buffer, size);
            break;

        default:
            printf("Not implemented syscall.\n");
            // This system call has not been implemented
            sys_nosys();
    }
    thread_exit();
}
