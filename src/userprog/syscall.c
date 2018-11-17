#include "userprog/syscall.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/vaddr.h"
#include "pagedir.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "devices/input.h"

static void syscall_handler(struct intr_frame *);
uint32_t peek_stack(struct intr_frame *f, int back);

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

/* Converts a file descriptor to the corresponding file struct given a
    thread that owns the file descriptor. */
struct file *fd_to_file(struct thread *t, int fd) {
    // Loop over the files for this thread and see if any of them match fd
    struct list_elem *e;
    for (e = list_begin(&t->files); e != list_end(&t->files); e = list_next(e)){
        struct file_descriptor *file_desp =
            list_entry(e, struct file_descriptor, elem);
        if (file_desp->fd == fd) {// We found the file with this descriptor
            return file_desp->file;
        }
    }
    return NULL;
}

/* Converts a file descriptor to the corresponding file descriptor struct given
    thread that owns the file descriptor. */
struct file_descriptor *fd_to_file_desc(struct thread *t, int fd) {
    // Loop over the files for this thread and see if any of them match fd
    struct list_elem *e;
    for (e = list_begin(&t->files); e != list_end(&t->files); e = list_next(e)){
        struct file_descriptor *file_desp =
            list_entry(e, struct file_descriptor, elem);
        if (file_desp->fd == fd) {// We found the file with this descriptor
            return file_desp;
        }
    }
    return NULL;
}

/* Closes all open file descriptors for a particular thread. */
void close_files(struct thread *t) {
    while (!list_empty(&t->files)) {
        list_pop_front(&t->files);
    }
    return;
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
     for (e = list_begin(&thread_current()->children);
          e != list_end(&thread_current()->children); e = list_next(e)) {
        c = list_entry(e, struct child_process, elem);
        c->child->parent = NULL;
    }

    printf ("%s: exit(%d)\n", thread_current()->name, status);
    sema_up(&c->signal);
 }

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

int sys_exec(const char *file_name) {
    tid_t id = process_execute(file_name);
    return id;
}

// TODO: tid_t should be pid_t? But process_wait wants tid_t...
int sys_wait(tid_t pid) {
    // while (1) {}
    return process_wait(pid);
}

/* Creates a new file called file initially initial_size bytes in size. Returns
    true if successful, false otherwise. Does not open the file. */
bool sys_create(const char *file_name, unsigned initial_size) {
    // Check the file name
    if (!valid_pointer(file_name)) {
        sys_exit(-1);
    }
    return filesys_create(file_name, initial_size);
}

/* Deletes the file called file. Returns true if successful, false otherwise. A
    file may be removed regardless of whether it is open or closed, and removing
    an open file does not close it. */
bool sys_remove(const char *file_name) {
    return filesys_remove(file_name);
}

/* Opens the file with file_name and returns a file descriptor. */
int sys_open(const char *file_name) {
    struct thread *t = thread_current();
    // Check the file name
    if (!valid_pointer(file_name)) {
        sys_exit(-1);
    }
    // Check if this thread has already called open on this file
    struct list_elem *e;
    struct file_descriptor *open_file;
    struct file *file_struct;
    for (e = list_begin(&t->files); e != list_end(&t->files); e = list_next(e)){
        open_file = list_entry(e, struct file_descriptor, elem);
        if (strcmp(open_file->file_name, file_name) == 0) {// This file is open
            return file_reopen(open_file->file);
        }
    }
    // If open has not already been called, call open to open it
    file_struct = filesys_open(file_name);
    if (!file_struct) {// If the file could not be opened
        return -1;
    }
    struct file_descriptor *file_desp = palloc_get_page(PAL_ZERO);
    file_desp->fd = t->fd++;
    file_desp->file_name = file_name;
    file_desp->file = file_struct;
    // Append to the file descriptor list for this thread
    list_push_back(&t->files, &file_desp->elem);
    return file_desp->fd;
}

/* Returns the size, in bytes, of the file open as fd. If file not found,
    returns -1. */
int sys_filesize(int fd) {
    struct file *file = fd_to_file(thread_current(), fd);
    if (!file) {// This thread does not have this file descriptor
        return -1;
    }
    return file_length(file);
}

/* Reads a total of size bytes from the file open as fd into buffer. Returns
    the total number of bytes read. 0 if at the end of the file, -1 if the
    file could not be read. */
int sys_read(int fd, const void *buffer, unsigned size) {
    if (fd == 0) {// Need to read from the keyboard
        unsigned int i = 0;
        while (i < size) {
            *((char*)buffer++) = input_getc();
        }
        return size;
    }
    // Check the buffer
    if (!valid_pointer(buffer)) {
        sys_exit(-1);
    }
    // Read from a file
    struct file *file = fd_to_file(thread_current(), fd);
    if (!file) {// This thread does not have this file descriptor
        return -1;
    }
    int32_t bytes_read = file_read(file, buffer, size);
    if ((unsigned) bytes_read < size) {
        return 0;
    } else {
        return bytes_read;
    }
}

/* System call for writing to a file descriptor. Writes directly to the console
    if fd is set to 1. Returns the number of bytes written. */
int sys_write(int fd, const void *buffer, unsigned size) {
    // Check the buffer
    if (!valid_pointer(buffer)) {
        sys_exit(-1);
    }
    if (fd == 1) {// Need to write to the console
        putbuf(buffer, size);
        return size;
    }
    // Write to the file
    struct file *file = fd_to_file(thread_current(), fd);
    if (!file) {// Did not find a file for this thread
        return -1;
    }
    int bytes_wrote = file_write(file, buffer, size);
    return bytes_wrote;
}

/* Changes the next byte to be read or written in open file fd to position,
    expressed in bytes from the beginning of the file. */
void sys_seek(int fd, unsigned position) {
    struct file *file = fd_to_file(thread_current(), fd);
    if (!file) {
        return;
    }
    file_seek(file, position);
    return;
}

/* Returns the position of the next byte to be read or written in open file fd,
    expressed in bytes from the beginning of the file. */
unsigned sys_tell(int fd) {
    struct file *file = fd_to_file(thread_current(), fd);
    if (!file) {
        return 0;
    }
    return file_tell(file);
}

/* Closes file descriptor fd. Exiting or terminating a process implicitly closes
  all its open file descriptors, as if by calling this function for each one. */
void sys_close(int fd) {
    struct file_descriptor *file_desp = fd_to_file_desc(thread_current(), fd);
    if (!file_desp) {// Couldn't find this file
        return;
    }
    // Need to remove from the thread_current list
    list_remove(&file_desp->elem);
    // Free memory
    palloc_free_page(file_desp);
    return;
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
    unsigned int size, initial_size, position;
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

        case SYS_CREATE:
            file_name = (const char *) peek_stack(f, 1);
            initial_size = (unsigned int) peek_stack(f, 2);
            f->eax = sys_create(file_name, initial_size);
            break;

        case SYS_REMOVE:
            file_name = (const char *) peek_stack(f, 1);
            f->eax = sys_remove(file_name);
            break;

        case SYS_OPEN:
            file_name = (const char *) peek_stack(f, 1);
            f->eax = sys_open(file_name);
            break;

        case SYS_FILESIZE:
            fd = (int) peek_stack(f, 1);
            f->eax = sys_filesize(fd);
            break;

        case SYS_READ:
            fd = (int) peek_stack(f, 1);
            buffer = (const void *) peek_stack(f, 2);
            size = (unsigned) peek_stack(f, 3);
            f->eax = sys_read(fd, buffer, size);
            break;

        case SYS_WRITE:
            // Arguments should be pushed in reverse order
            fd = (int) peek_stack(f, 1);
            buffer = (void *) peek_stack(f, 2);
            size = (unsigned int) peek_stack(f, 3);
            f->eax = sys_write(fd, buffer, size);
            break;

        case SYS_SEEK:
            fd = (int) peek_stack(f, 1);
            position = (unsigned) peek_stack(f, 2);
            sys_seek(fd, position);
            break;

        case SYS_TELL:
            fd = (int) peek_stack(f, 1);
            f->eax = sys_tell(fd);
            break;

        case SYS_CLOSE:
            fd = (int) peek_stack(f, 1);
            sys_close(fd);
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
