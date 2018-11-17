#include "userprog/syscall.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
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

/* Peeks/accesses an element from the interrupt stack frame passed in. */
uint32_t peek_stack(struct intr_frame *f, int back) {
    uint32_t elem;
    uint32_t *addr = (uint32_t *) f->esp + back;
    // If the esp points to a bad address, immediately do exit(-1)
    if (!valid_pointer(addr)) {
        sys_exit(-1);
    }
    elem = *addr;
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
     struct child_process *c = get_child_process(thread_current()->parent,
        thread_current()->tid);
     if (thread_current()->parent) {
         c->exit_status = status;
     }

     // Iterate over all of your children and set all their parents to NULL
     struct list_elem *e;
     struct child_process *d;
     for (e = list_begin(&thread_current()->children);
          e != list_end(&thread_current()->children); e = list_next(e)) {
        d = list_entry(e, struct child_process, elem);
        d->child->parent = NULL;
    }

    printf ("%s: exit(%d)\n", thread_current()->name, status);
    //printf("c signal value: %d\n", c->signal.value);
    //printf("c child name: %s\n", c->child->name);
    //printf("Current thread name: %s\n", thread_current()->name);
    sema_up(&c->signal);
    //printf("c signal value: %d\n", c->signal.value);
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
//         // TODO: This needs to be checkee
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
    exit_with_status(status);
    thread_exit();
}

// TODO: tid_t should be pid_t? But process_wait wants tid_t...
int sys_wait(tid_t pid) {
    return process_wait(pid);
}

int sys_exec(const char *file_name) {
    // If the filename is invalid, immediately return pid -1
    if (! valid_pointer(file_name)) {
        return -1;
    }
    tid_t id = process_execute(file_name);
    return id;
}

/* System call for writing to a file descriptor. Writes directly to the console
    if fd is set to 1. Returns the number of bytes written. */
int sys_write(int fd, const void *buffer, unsigned size) {
    // Check that the buffer is a valid pointer and if not return
    if (!valid_pointer((void*) buffer)) {
        // TODO: Not sure if this should exit or return 0 or both
        thread_exit();
        // sys_exit(-1); // Not sure if it should be this or thread_exit()
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
    const char *file_name;
    unsigned int size;
    int status;
    tid_t pid;
    // Peek the syscall number from the interrupt stack frame
    uint32_t call_num = peek_stack(f, 0);
    // printf("Call num: %d\n", call_num);
    switch (call_num) {
        // Terminate pintos by calling shutdown_power_off() in shutdown.h
        case SYS_HALT:
            // printf("Halt syscall!\n");
            sys_halt();
            NOT_REACHED();

        case SYS_EXIT:
            // printf("Exit syscall\n");
            status = (int) peek_stack(f, 1);
            sys_exit(status);
            break;

        case SYS_EXEC:
            // printf("Exec syscall\n");
            file_name = (const char *) peek_stack(f, 1);
            f->eax = sys_exec(file_name);
            break;

        case SYS_WAIT:
            // printf("Wait syscall\n");
            pid = peek_stack(f, 1);
            f->eax = sys_wait(pid);
            break;

        case SYS_WRITE:
            // printf("Write syscall!\n");
            // Arguments should be pushed in reverse order
            fd = (int) peek_stack(f, 1);
            buffer = (void *) peek_stack(f, 2);
            size = (unsigned int) peek_stack(f, 3);
            // Write to the file or console as determined by fd
            f->eax = sys_write(fd, buffer, size);
            break;

        default:
            // printf("ENOSYS: Function not implemented.\n");
            // This system call has not been implemented
            // sys_nosys();
            sys_halt();
            thread_exit();
    }
    return;
}
