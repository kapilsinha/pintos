#include "userprog/syscall.h"
#include "userprog/process.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/vaddr.h"
#include "pagedir.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "devices/input.h"
#include "threads/malloc.h"
#include "lib/debug.h"

static void syscall_handler(struct intr_frame *f);
uint32_t peek_stack(struct intr_frame *f, int back);

/*!
 *  Check if the virtual address pointer is a user virtual address and that the
 *  virtual address is mapped to physical memory.
 *  is_user_vaddr (defined in threads/vaddr.h) simply checks if vaddr is
 *  less than the PHYS_BASE
 *  find_entry (defined in page.c) checks whether the virtual address contains
 *  some mapping in the supplemental page table.
 *  Returns NULL if vaddr is unmapped in the page directory
 *
 *  If there exists no entry but the virtual address indicates that it is
 *  trying to grow the stack, a stack page is added to the supp page table
 */
int valid_pointer(void *vaddr, struct intr_frame *f) {
    if (! is_user_vaddr(vaddr)) {
        return false;
    }

    uint8_t *upage = pg_round_down(vaddr);
    if (find_entry(upage, thread_current())) {
        return true;
    }
    if (valid_stack_growth(vaddr, f)) {
        supp_add_stack_entry(upage);
        return true;
    }
    return false;
}

/*!
 *  Performs the same function as valid_pointer, but efficiently ensures
 *  that each page in some range of addresses (e.g. a buffer) are valid,
 *  again growing the stack if the start address indicates that it is
 *  trying to grow the stack
 */
int valid_pointer_range(void *vaddr, unsigned size, struct intr_frame *f) {
    if (! is_user_vaddr(vaddr) || ! is_user_vaddr(vaddr + size)) {
        return false;
    }
    void *cur_addr = vaddr;
    bool stack_growth = valid_stack_growth(vaddr, f);
    while (cur_addr < vaddr + size) {
        uint8_t *upage = pg_round_down(cur_addr);
        struct supp_page_table_entry * entry = find_entry(upage, thread_current());
        if (! entry && ! stack_growth) {
            return false;
        }
        if (! entry && stack_growth) {
            supp_add_stack_entry(upage);
        }
        cur_addr = upage + PGSIZE;
    }
    return true;
}

/*! Finds the corresponding supplemental page table entry for a given user page
 *  and thread. If none are found, returns NULL.
 */
struct mmap_table_entry *find_mmap_entry(mapid_t mapping, struct thread *t) {
    struct mmap_table_entry to_find;
    to_find.mapping = mapping;
    struct hash_elem *e = hash_find(&t->mmap_file_table, &to_find.elem);
    return e != NULL ? hash_entry(e, struct mmap_table_entry, elem) : NULL;
}

/*! Peeks/accesses an element from the interrupt stack frame passed in. */
uint32_t peek_stack(struct intr_frame *f, int back) {
    uint32_t elem;
    uint32_t *addr = (uint32_t *) f->esp + back;
    /* If the esp points to a bad address, immediately do exit(-1) */
    if (!valid_pointer((void *) addr, f)) {
        sys_exit(-1, f);
    }
    elem = *addr;
    return elem;
}

/*!
 *  Converts a file descriptor to the corresponding file descriptor struct
 *  given thread that owns the file descriptor.
 *  Returns NULL if the file descriptor is not found for the thread.
 */
struct file_descriptor *fd_to_file_desc(struct thread *t, int fd) {
    /* Loop over the files for this thread and see if any of them match fd */
    struct list_elem *e;
    for (e = list_begin(&t->files); e != list_end(&t->files);
         e = list_next(e)){
        struct file_descriptor *file_desp =
            list_entry(e, struct file_descriptor, elem);
        if (file_desp->fd == fd) {// We found the file with this descriptor
            return file_desp;
        }
    }
    return NULL;
}

/*!
 *  Converts a file descriptor to the corresponding file struct given a
 *  thread that owns the file descriptor.
 *  Returns NULL if the file descriptor is not found for the thread.
 */
struct file *fd_to_file(struct thread *t, int fd) {
    struct file_descriptor *file_desp = fd_to_file_desc(t, fd);
    if (file_desp == NULL) {
        return NULL;
    }
    return file_desp->file;
}

/*!
 *  Prints process name (the full name passed to process_execute without
 *  the command-line arguments) and the exit status
 *  Also accesses this process' parent and sets its exit status and sema_up
 *  This method is called whenever a user process terminates (either via the
 *  exit syscall or through process.c/kill but not in the halt syscall) and so
 *  a call to this function is followed by a call to thread_exit)
 */
void exit_with_status(int status) {
    /*
     * If the parent is dead, this process's parent pointer should already
     * point to NULL - in which case you don't have to update the parent
     */
    struct child_process *c = thread_get_child_process
        (thread_current()->parent, thread_current()->tid);
    if (thread_current()->parent) {
        c->exit_status = status;
    }

    /* Iterate over all of your children and set all their parents to NULL */
    struct list_elem *e;
    struct child_process *d;
    for (e = list_begin(&thread_current()->children);
        e != list_end(&thread_current()->children); e = list_next(e)) {
        d = list_entry(e, struct child_process, elem);
        d->child->parent = NULL;
    }

    printf ("%s: exit(%d)\n", thread_current()->name, status);

    /* Close all open files */
    while (!list_empty(&thread_current()->files)) {
        e = list_pop_back(&thread_current()->files);
        struct file_descriptor *f
            = list_entry(e, struct file_descriptor, elem);
        file_close(f->file);
        list_remove(&f->elem);
        /* Free the file descriptor's memory */
        free(f);
    }

    /* Munmap all mmap files */
    struct hash_iterator i;
    hash_first (&i, &thread_current()->mmap_file_table);
    while (hash_next (&i)) {
        struct mmap_table_entry *entry
            = hash_entry (hash_cur (&i), struct mmap_table_entry, elem);
        sys_munmap(entry->mapping, NULL);
    }
    hash_destroy(&thread_current()->mmap_file_table, &hash_free_mmap_entry);

    sema_up(&c->signal);
 }

void syscall_init(void) {
    intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/*!
 *  Terminates Pintos by calling shutdown_power_off()
 *  (declared in devices/shutdown.h). This should be seldom used, because you
 *  lose some information about possible deadlock situations, etc.
 */
void sys_halt(struct intr_frame *f UNUSED) {
    shutdown_power_off();
    NOT_REACHED();
}

/*!
 *  Terminates a thread by deleting its child_process struct from its
 *  parent and printing its exit message and finaly calling thread_exit.
 */
void sys_exit(int status, struct intr_frame *f UNUSED) {
    exit_with_status(status);
    thread_exit();
}

/*!
 *  Spawns a child process to execute a file and returns its pid.
 *  The child is forced to load and if it fails to load, it returns -1.
 *  Return -1 if file not found.
 */
int sys_exec(const char *file_name, struct intr_frame *f) {
    /* If the filename is invalid, immediately return pid -1 */
    if (! valid_pointer((void *) file_name, f)) {
        return -1;
    }
    tid_t id = process_execute(file_name);
    if (id == TID_ERROR) {
        id = -1;
    }
    return id;
}

/*!
 *  Waits for process pid to terminate and returns its exit status.
 *  Note we simply use a one-to-one mapping between pid and tid
 *  for simplicity
 */
int sys_wait(tid_t pid, struct intr_frame *f UNUSED) {
    return process_wait(pid);
}

/*!
 *  Creates a new file called file initially initial_size bytes in size. Returns
 *  true if successful, false otherwise. Does not open the file.
 */
bool sys_create(const char *file_name, unsigned initial_size, struct intr_frame *f) {
    /* If the filename is invalid, immediately exit */
    if (! valid_pointer((void *) file_name, f) || strlen(file_name) == 0) {
        sys_exit(-1, f);
    }
    struct thread *t = thread_current();

    struct short_path *sp = get_dir_from_path(t->cur_dir, file_name);
    bool ret = false;
    /* Ensure that the path did not correspond to a directory */
    ASSERT(! sp->is_dir);
    if (sp->dir && sp->filename) {
        ret = filesys_create(sp->dir, sp->filename, initial_size);
        dir_close(sp->dir);
        free((char *) sp->filename);
    }
    free(sp);
    return ret;
}

/*!
 *  Deletes the file called file. Returns true if successful, false otherwise. A
 *  file may be removed regardless of whether it is open or closed, and removing
 *  an open file does not close it.
 *  TODO: Update this function to remove directories. The logic I think should be
 *  to return false automatically if there is stuff in the directory
 *  (inode->length != 0 I think?). Otherwise, do the same shit as before
 *  (call filesys_remove, which removes it from the parent directory and then on
 *  process exit, if the open_cnt == 0, the directory is file_closed and actually
 *  deleted). BUT you have to check in all open and current that the curr_dir is
 *  not removed i.e. if dir->inode->removed == true, then disallow opens and
 *  creates
 */
bool sys_remove(const char *file_name, struct intr_frame *f UNUSED) {
    struct thread *t = thread_current();
    struct short_path *sp = get_dir_from_path(t->cur_dir, file_name);
    bool ret = false;
    if (sp->dir && sp->filename) {
        if (! sp->is_dir) {
            /* If the path corresponds to an ordinary file, simply remove it */
            ret = filesys_remove(sp->dir, sp->filename);
        }
        else if (dir_get_length(sp->dir) == 0) {
            /* If the path corresponds to a directory, remove it only if it
             * is empty */
            ret = filesys_remove(dir_get_parent_dir(sp->dir), sp->filename);
        }
        dir_close(sp->dir);
        free((char *) sp->filename);
    }
    free(sp);
    return ret;
}

/*! Opens the file or directory with file_name and returns a file descriptor.
 *  A directory is treated like a file in that it has an inode and pos we 
 *  store, so this function treats directories as ordinary files without
 *  loss of generality
 *  TODO: Currently we didn't change open to handle directories - change it
 *  if it is required (but I think it's not) */
int sys_open(const char *file_name, struct intr_frame *f) {
    /* If the filename is invalid, immediately exit */
    if (! valid_pointer((void *) file_name, f)) {
        sys_exit(-1, f);
    }
    /* If the filename is empty, just return immediately */
    if (strlen(file_name) == 0)
        return -1;
    struct thread *t = thread_current();
    /*
     * If this thread has already opened this file, reopen it (with a new
     * file struct to track position and deny_write but same inode object)
     */
    struct list_elem *e;
    bool opened_previously = false;
    struct file_descriptor *open_file;
    struct file *file_struct;
    struct file_descriptor *file_desp
        = malloc(sizeof(struct file_descriptor));
    if (! file_desp) {
        printf("Malloc failed in open\n");
        return -1;
    }
    struct short_path *sp = get_dir_from_path(t->cur_dir, file_name);
    if (! sp->dir || ! sp->filename) {
        free(sp);
        return -1;
    }

    // bool val = true;
    // char *name = malloc(NAME_MAX + 1);
    // while (val) {
    //     val = dir_readdir(sp->dir, name);
    //     printf("Name: %s\n", name);
    // }
    // free(name);
    /*
     * Iterate over all files this thread has opened for the current file we
     * seek to open
     */
    for (e = list_begin(&t->files); e != list_end(&t->files);
         e = list_next(e)) {
        open_file = list_entry(e, struct file_descriptor, elem);
        if (strcmp(open_file->file_name, sp->filename) == 0) { // This file is open
            opened_previously = true;
            file_desp->file = file_reopen(open_file->file);
            break;
        }
    }

    /* If this thread has not previously opened this file, call open */
    if (! opened_previously) {
        if (sp->is_dir) {
            file_struct = filesys_open(dir_get_parent_dir(sp->dir), sp->filename);
        }
        else {
            file_struct = filesys_open(sp->dir, sp->filename);
        }
        if (! file_struct) { // If the file could not be opened
            return -1;
        }
        file_desp->file = file_struct;
    }

    file_desp->fd = t->fd_next++;
    file_desp->file_name = (char *) sp->filename;

    dir_close(sp->dir);
    free((char *) sp->filename);
    free(sp);

    /* Append to the file descriptor list for this thread */
    list_push_back(&t->files, &file_desp->elem);

    return file_desp->fd;
}

/*!
 *  Returns the size, in bytes, of the file open as fd.
 *  If file not found, returns -1.
 */
int sys_filesize(int fd, struct intr_frame *f UNUSED) {
    struct file *file = fd_to_file(thread_current(), fd);
    if (! file) {
        return -1;
    }
    return file_length(file);
}

/*! Reads a total of size bytes from the file open as fd into buffer. Returns
 *  the total number of bytes read. 0 if at the end of the file, -1 if the
 *  file could not be read.
 */
int sys_read(int fd, const void *buffer, unsigned size, struct intr_frame *f) {
    /* If the buffer is invalid, immediately exit */
    if (!valid_pointer_range((void *) buffer, size, f) || fd == 1) {
        sys_exit(-1, f);
    }
    if (fd == 0) { // STDIN
        unsigned int i = 0;
        while (i < size) {
            *((char*)buffer++) = input_getc();
        }
        return size;
    }

    /* Read from the file */
    struct file *file = fd_to_file(thread_current(), fd);
    if (! file) {
        return -1;
    }
    int32_t bytes_read = file_read(file, (void *) buffer, size);
    return bytes_read;
}

/*!
 *  System call for writing to a file descriptor. Writes directly to the
 *  console if fd is set to 1. Returns the number of bytes written.
 *  If file not found, returns 0
 */
int sys_write(int fd, const void *buffer, unsigned size, struct intr_frame *f) {
    /* If the buffer is invalid, immediately exit */
    if (!valid_pointer_range((void *) buffer, size, f) || fd == 0) {
        sys_exit(-1, f);
    }
    /* If the file descriptor corresponds to a directory, we cannot write to
     * it, so we immediately exit */
    if (sys_isdir(fd, f)) {
        sys_exit(-1, f);
    }
    if (fd == 1) { // STDOUT
        putbuf(buffer, size);
        return size;
    }

    /* Write to the file */
    struct file *file = fd_to_file(thread_current(), fd);
    /* If this thread does not have this file, return 0 */
    if (! file) {
        return 0;
    }
    int bytes_wrote = file_write(file, buffer, size);
    return bytes_wrote;
}

/*!
 *  Changes the next byte to be read or written in open file fd to position,
 *  expressed in bytes from the beginning of the file.
 */
void sys_seek(int fd, unsigned position, struct intr_frame *f UNUSED) {
    struct file *file = fd_to_file(thread_current(), fd);
    if (! file) {
        return;
    }
    file_seek(file, position);
    return;
}

/*!
 *  Returns the position of the next byte to be read or written in open file fd,
 *  expressed in bytes from the beginning of the file.
 *  If file not found, returns 0
 */
unsigned sys_tell(int fd, struct intr_frame *f UNUSED) {
    struct file *file = fd_to_file(thread_current(), fd);
    if (! file) {
        return 0;
    }
    return file_tell(file);
}

/*!
 *  Closes file descriptor fd. Exiting or terminating a process implicitly closes
 *  all its open file descriptors, as if by calling this function for each one.
 */
void sys_close(int fd, struct intr_frame *f UNUSED) {
    struct file_descriptor *file_desp = fd_to_file_desc(thread_current(), fd);
    if (! file_desp) {// Couldn't find this file
        return;
    }
    /* Close the file */
    file_close(file_desp->file);
    /* Remove the file from the thread_current list */
    list_remove(&file_desp->elem);
    /* Free the file descriptor struct's memory */
    free(file_desp);
    return;
}

/*!
 *  Maps the file open at fd into the process' virtual address space.
 *  Note: the file is mapped in consecutive virtual pages
 */
mapid_t sys_mmap(int fd, void *addr, struct intr_frame *f UNUSED) {
    struct thread *t = thread_current();
    /* Stdin and stdout are not mappable */
    if (fd == 0 || fd == 1) {
        return -1;
    }

    /* Get file struct */
    struct file *original_file = fd_to_file(thread_current(), fd);
    if (! original_file) {
        return -1;
    }

    /* Fail if the file has a length of 0 */
    off_t file_size = file_length(original_file);
    if (file_size == 0) {
        return -1;
    }

    /* Fail if addr is at 0 or is not page-aligned */
    if (addr == 0 || addr != pg_round_down(addr)) {
        return -1;
    }

    /* Fail if any address in [addr, addr + file_size] is already mapped */
    void *cur_addr = addr;
    int num_pages = 0;       // Number of virtual pages to allocate for the mmap
    int last_page_bytes = 0; // Number of bytes to write to the final page
    while (cur_addr < addr + file_size) {
        /* If the cur_addr is already mapped, find_entry returns non-NULL
         * and so we fail */
        if (! is_user_vaddr(cur_addr) || find_entry(cur_addr, t)) {
            return -1;
        }
        num_pages++;
        last_page_bytes = addr + file_size - cur_addr;
        cur_addr += PGSIZE;
    }

    /* Mapping must remain valid until munmap is called or process exists,
     * so we obtain a new reference to the file for this mapping */
    struct file *file = file_reopen(original_file);

    /* Now we know the mapping is valid, so we add it to the mmap_file_table */
    struct mmap_table_entry * mmap_entry
        = malloc(sizeof(struct mmap_table_entry));
    if (mmap_entry == NULL) {
        PANIC("Malloc of mmap_entry in sys_map failed");
    }
    mmap_entry->mapping = t->mapping_next;
    t->mapping_next++;
    mmap_entry->page_addr = addr;
    mmap_entry->file_size = file_size;
    mmap_entry->fd = fd;
    mmap_entry->file = file;
    struct hash_elem * mmap_elem
        = hash_insert(&t->mmap_file_table, &mmap_entry->elem);
    /* This mapping should not be present in the hash table;
     * if it is, there may be repeated mappings */
    if (mmap_elem != NULL) {
        PANIC("This mmap mapping was already in the mmap hash table");
    }

    void *upage = addr;
    int page_index = 0;
    bool writable = true;
    while (upage < addr + file_size) {
        /* Calculate how to fill this page.
           We will read PAGE_READ_BYTES bytes from FILE
           and zero the final PAGE_ZERO_BYTES bytes. */
        size_t page_read_bytes
            = page_index == num_pages - 1 ? last_page_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        supp_add_mmap_entry(file, page_read_bytes, page_zero_bytes,
                            writable, upage);

        /* Advance. */
        upage += PGSIZE;
        page_index++;
    }

    return mmap_entry->mapping;
}

/*!
 *  Unmaps the mapping designated by the mapid_t that was returned
 *  by the mmap syscall
 */
void sys_munmap(mapid_t mapping, struct intr_frame *f UNUSED) {
    struct thread *t = thread_current();
    struct mmap_table_entry *mmap_entry
        = find_mmap_entry(mapping, thread_current());
    void *addr = mmap_entry->page_addr;
    off_t file_size = mmap_entry->file_size;

    /* Determine if we have written to the file */
    void *upage = addr;
    bool dirty = false;
    while (upage < addr + file_size) {
        if (pagedir_is_dirty(thread_current()->pagedir, upage)) {
            dirty = true;
            break;
        }
        upage += PGSIZE;
    }

    /* Go to the start of the file and write the contents from upage
     * if data was modified in memory */
    if (dirty) {
        file_seek(mmap_entry->file, 0);
        file_write(mmap_entry->file, addr, file_size);
    }

    /* Close the files since we had reopened it earlier */
    file_close(mmap_entry->file);

    /* Delete the entries in both mmap and supplemental page tables */
    hash_delete(&t->mmap_file_table, &mmap_entry->elem);

    /* Remove the elements in the supplemental page table corresponding
     * to the virtual addresses that the mmap file had occupied */
    upage = addr;
    while (upage < addr + file_size) {
        void *frame = pagedir_get_page(t->pagedir, upage);
        if (frame) {
            frame_free_page(frame);
        }
        hash_delete(&t->supp_page_table, &find_entry(upage, t)->elem);
        upage += PGSIZE;
    }
}


/*! Changes the current working directory of the current process to
 *  the argument DIR. Returns true on success, false on failure.
 */
bool sys_chdir(const char *dir, struct intr_frame *f UNUSED) {    
    struct thread *t = thread_current();
    struct short_path *sp = get_dir_from_path(t->cur_dir, dir);
    bool ret = false;
    if (sp->is_dir && sp->dir && sp->filename) {
        dir_close(t->cur_dir);
        t->cur_dir = sp->dir;
        free((char *) sp->filename);
        ret = true;
    }
    free(sp);
    return ret;
}

/*! Creates a directory named DIR (relative or absolute path).
 *  Returns true on success and false on failure
 *  Failure means DIR already exists or if any directory name
 *  in DIR does not already exist
 */
bool sys_mkdir(const char *dir, struct intr_frame *f UNUSED) {
    /*
    if (! valid_pointer((void *) dir, f)) {
        sys_exit(-1, f);
    }
    */
    struct thread *t = thread_current();
    struct short_path *sp = get_dir_from_path(t->cur_dir, dir);
    bool ret = false;
    if (! sp->is_dir && sp->dir && sp->filename) {
        ret = filesys_mkdir(sp->dir, sp->filename);
        dir_close(sp->dir);
        free((char *) sp->filename);
    }
    free(sp);
    return ret;
}

/*! Reads a directory entry from file descriptor FD (which must be
 *  a directory).
 *  If successful, returns true and stores the file name in NAME
 *  Returns false on failure
 */
bool sys_readdir(int fd, char *name, struct intr_frame *f) {
    /* Ensure that fd corresponds to a directory */
    ASSERT(sys_isdir(fd, f));
    struct file *file = fd_to_file(thread_current(), fd);
    struct dir *dir = dir_open(file_get_inode(file));
    return dir_readdir(dir, name);
    /* TODO: When can I close the directory??? */
    //dir_close(dir);
}

/*! Returns true if fd represents a directory,
 *  false if fd represents an ordinary file
 */
bool sys_isdir(int fd, struct intr_frame *f UNUSED) {
    struct file *file = fd_to_file(thread_current(), fd);
    return file && file_isdir(file);
}

/*!
 *  Returns the inode number of the inode associated with fd
 *  (which may be an ordinary file or a directory). We use the
 *  sector number as the inode number
 */
int sys_inumber(int fd, struct intr_frame *f UNUSED) {
    struct file *file = fd_to_file(thread_current(), fd);
    if (! file) {
        return -1;
    }
    return file_inumber(file);
}

/*! Called for system calls that are not implemented. */
void sys_nosys(struct intr_frame *f UNUSED) {
    printf("ENOSYS: Function not implemented.\n");
    sys_exit(-1, f);
    return;
}

/*!
 *  Handles all syscalls by redirecting to the appropriate sys_function
 *  based on the call_num on the stack
 */
static void syscall_handler(struct intr_frame *f) {
    int fd;
    const void *buffer;
    const char *file_name;
    const char *dir;
    char *name;
    unsigned int size, initial_size, position;
    int status;
    tid_t pid;
    void *addr;
    mapid_t mapping;
    /* Peek the syscall number from the interrupt stack frame */
    uint32_t call_num = peek_stack(f, 0);
    switch (call_num) {
        /*
         * Terminate pintos by calling shutdown_power_off() in shutdown.h
         * Arguments should be pushed in reverse order
         */
        case SYS_HALT:
            sys_halt(f);
            NOT_REACHED();

        case SYS_EXIT:
            status = (int) peek_stack(f, 1);
            sys_exit(status, f);
            break;

        case SYS_EXEC:
            file_name = (const char *) peek_stack(f, 1);
            f->eax = sys_exec(file_name, f);
            break;

        case SYS_WAIT:
            pid = peek_stack(f, 1);
            f->eax = sys_wait(pid, f);
            break;

        case SYS_CREATE:
            file_name = (const char *) peek_stack(f, 1);
            initial_size = (unsigned int) peek_stack(f, 2);
            f->eax = sys_create(file_name, initial_size, f);
            break;

        case SYS_REMOVE:
            file_name = (const char *) peek_stack(f, 1);
            f->eax = sys_remove(file_name, f);
            break;

        case SYS_OPEN:
            file_name = (const char *) peek_stack(f, 1);
            f->eax = sys_open(file_name, f);
            break;

        case SYS_FILESIZE:
            fd = (int) peek_stack(f, 1);
            f->eax = sys_filesize(fd, f);
            break;

        case SYS_READ:
            fd = (int) peek_stack(f, 1);
            buffer = (const void *) peek_stack(f, 2);
            size = (unsigned) peek_stack(f, 3);
            f->eax = sys_read(fd, buffer, size, f);
            break;

        case SYS_WRITE:
            fd = (int) peek_stack(f, 1);
            buffer = (void *) peek_stack(f, 2);
            size = (unsigned int) peek_stack(f, 3);
            f->eax = sys_write(fd, buffer, size, f);
            break;

        case SYS_SEEK:
            fd = (int) peek_stack(f, 1);
            position = (unsigned) peek_stack(f, 2);
            sys_seek(fd, position, f);
            break;

        case SYS_TELL:
            fd = (int) peek_stack(f, 1);
            f->eax = sys_tell(fd, f);
            break;

        case SYS_CLOSE:
            fd = (int) peek_stack(f, 1);
            sys_close(fd, f);
            break;

        case SYS_MMAP:
            fd = (int) peek_stack(f, 1);
            addr = (void *) peek_stack(f, 2);
            f->eax = sys_mmap(fd, addr, f);
            break;

        case SYS_MUNMAP:
            mapping = (mapid_t) peek_stack(f, 1);
            sys_munmap(mapping, f);
            break;

        case SYS_CHDIR:
            dir = (const char *) peek_stack(f, 1);
            f->eax = sys_chdir(dir, f);
            break;

        case SYS_MKDIR:
            dir = (const char *) peek_stack(f, 1);
            f->eax = sys_mkdir(dir, f);
            break;

        case SYS_READDIR:
            fd = (int) peek_stack(f, 1);
            name = (char *) peek_stack(f, 2);
            f->eax = sys_readdir(fd, name, f);
            break;

        case SYS_ISDIR:
            fd = (int) peek_stack(f, 1);
            f->eax = sys_isdir(fd, f);
            break;

        case SYS_INUMBER:
            fd = (int) peek_stack(f, 1);
            f->eax = sys_inumber(fd, f);
            break;

        default:
            sys_nosys(f);
            NOT_REACHED();
    }
    return;
}
