#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "pagedir.h"
#include "devices/shutdown.h"

static void syscall_handler(struct intr_frame *);
uint32_t peek_stack(struct intr_frame *f, int back);

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

/* Check if the virtual address pointer is a user virtual address and that the
   virtual address is mapped to physical memory.
   is_user_vaddr (defined in threads/vaddr.h) simply checks if vaddr is
   less than the PHYS_BASE
   pagedir_get_page (defined in pagedir.c) looks up the physical address for
   vaddr. Returns NULL if vaddr is unmapped in the page directory or else
   returns the kernel virtual address corresponding to that physical address
 */
int valid_pointer(void *vaddr) {
    return (is_user_vaddr(vaddr) &&
            pagedir_get_page(thread_current()->pagedir, vaddr));
}

/* Pops an element from the interrupt stack frame passed in. */
// TODO: Access but do not pop
uint32_t peek_stack(struct intr_frame *f, int back) {
    uint32_t elem;
    elem = *((uint32_t *) f->esp + back);
    return elem;
}

 /*
  * Prints process name (the full name passed to process_execute without
  * the command-line arguments) and the exit status
  * Also accesses this process' parent and sets its exit status and sema_up
  * This method is called whenever a user process terminates (either via the
  * exit syscall or through process.c/kill but not in the halt syscall) and so
  * a call to this function is followed by a call to thread_exit)
  * Note: I placed this method here as opposed to thread.c or process.c because
  * the exit syscall is the only method with a status that must be printed
  * (the default otherwise is status -1).
  */
 void exit_with_status(int status) {
     // If the parent is dead, this process's parent pointer should already
     // point to NULL - in which case you don't have to update the parent
     struct child_process *c;
     if (thread_current()->parent) {
         c = get_child_process(thread_current()->parent, thread_current()->tid);
         // TODO: Get rid of the assert later
         assert (c->child->tid == thread_current()->tid);
         c->exit_status = status;
     }

     // Iterate over all of your children and set all their parents to NULL
     struct list_elem *e;
     for (e = list_begin(&thread_current()->children);
          e != list_end(&thread_current()->children); e = list_next(e)) {
        c = list_entry(e, struct child_process, elem);
        c->child->parent = NULL;
    }

    printf ("%s:exit(%d)\n", thread_current()->name, status);
    sema_up(&c->signal);
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

void sys_exit(int status) {
    // TODO: Implement this
    exit_with_status(status);
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
    return;
}

static void syscall_handler(struct intr_frame *f) {
    // printf("System call!\n");
    int fd;
    const void *buffer;
    unsigned int size;
    // Pop the syscall number from the interrupt stack frame
    uint32_t call_num = peek_stack(f, 0);
    switch (call_num) {
        // Terminate pintos by calling shutdown_power_off() in shutdown.h
        case SYS_HALT:
            // printf("Halt syscall!\n");
            sys_halt();
            NOT_REACHED();

        case SYS_WRITE:
            // printf("Write syscall!\n");
            // Arguments should be pushed in reverse order
            fd = (int) peek_stack(f, 1); // TODO: Change to peek
            buffer = (void *) peek_stack(f, 2);
            size = (unsigned int) peek_stack(f, 3);
            // Write to the file or console as determined by fd
            sys_write(fd, buffer, size);
            break;

        case SYS_WAIT:
            printf("Wait syscall\n");
            return;

        default:
            // printf("ENOSYS: Function not implemented.\n");
            // This system call has not been implemented
            sys_nosys();
            thread_exit();
    }
    return;
}
