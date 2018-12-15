#include "userprog/process.h"
#include "userprog/syscall.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"

#define WORD_SIZE 4
#define MAX_FILENAME_LENGTH 128

static thread_func start_process NO_RETURN;
static bool load(const char *cmdline, void (**eip)(void), void **esp);

/*!
 *  Returns the first word (separated by spaces) of a file_name.
 *  Returns NULL if malloc fails, so the return type must be checked
 */
const char *get_command_name(const char *file_name) {
    char *command_name = malloc(MAX_FILENAME_LENGTH * sizeof(char));
    if (!command_name) {
        return NULL;
    }
    int start_index = 0;
    int end_index = 0;
    int i = 0;
    /* Find the start and end index of the chars of the first word */
    while (file_name[i] != '\0') {
        if (file_name[i] == ' ') {
            if (start_index == i) {
                start_index++;
            }
            else {
                break;
            }
        }
        else {
            end_index = i;
        }
        i++;
    }
    /* Return NULL if the command name cannot fit in
     * MAX_FILENAME_LENGTH bytes */
    if (end_index - start_index + 1 > MAX_FILENAME_LENGTH) {
        return NULL;
    }
    for (int i = start_index; i <= end_index; i++) {
        command_name[i] = file_name[i];
    }
    command_name[end_index - start_index + 1] = '\0';
    return command_name;
}

/*!
 *  Starts a new thread running a user program loaded from FILENAME.  The new
 *  thread may be scheduled (and may even exit) before process_execute()
 *  returns.  Returns the new process's thread id, or TID_ERROR if the thread
 *  cannot be created.
 */
tid_t process_execute(const char *file_name) {
    char *fn_copy;
    tid_t tid;

    /*
     * The name of the thread must be just the command name without its
     * arguments (so the process termination message is printed correctly).
     */
    const char* command_name = get_command_name(file_name);

    /*
     * Make a copy of FILE_NAME.
     * Otherwise there's a race between the caller and load().
     * fn_copy is freed later in start_process
     */
    fn_copy = malloc(MAX_FILENAME_LENGTH * sizeof(char));
    if (fn_copy == NULL || command_name == NULL)
        return TID_ERROR;
    strlcpy(fn_copy, file_name, MAX_FILENAME_LENGTH);

    /*
     * Create a new thread to execute FILE_NAME, which adds the new thread's
     * child_process struct to the current thread
     */
    tid = child_thread_create(command_name, PRI_DEFAULT, start_process, fn_copy);
    struct child_process *child = thread_get_child_process(thread_current(), tid);

    /*
     * load_sema is downed by the current thread when it creates the child to
     * force the child to load
     */
    sema_down(&child->load_sema);
    if (tid == TID_ERROR)
        free((void *) command_name);

    /* If the file failed to load in start_proces, we set the tid to -1 */
    if (! child->is_load_successful) {
        tid = -1;
    }
    /*
     * parent_load_sema is upped by the current thread after it updates the
     * child's tid to allow the child to run
     */
    sema_up(&child->parent_load_sema);
    return tid;
}

/*! A thread function that loads a user process and starts it running. */
static void start_process(void *file_name_) {
    char *file_name = file_name_;
    struct intr_frame if_;
    bool success;

    /* Initialize interrupt frame and load executable. */
    memset(&if_, 0, sizeof(if_));
    if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
    if_.cs = SEL_UCSEG;
    if_.eflags = FLAG_IF | FLAG_MBS;
    success = load(file_name, &if_.eip, &if_.esp);
    free(file_name);


    struct child_process *child = thread_get_child_process
        (thread_current()->parent, thread_current()->tid);
    if (!success) {
        /* If load failed, quit. */
        child->is_load_successful = false;
        /* load_sema is upped by the current thread to allow the parent thread
         * to run, since it has finished loading */
        sema_up(&child->load_sema);
        /* parent_load_sema is downed by the current thread to force the
         * parent to update the current thread's tid */
        sema_down(&child->parent_load_sema);
        thread_exit();
    }
    else {
        child->is_load_successful = true;
        /* load_sema is upped by the current thread to allow the parent thread
         * to run, since it has finished loading */
        sema_up(&child->load_sema);
        /* parent_load_sema is downed by the current thread to force the
         * parent to update the current thread's tid */
        sema_down(&child->parent_load_sema);
    }

    /* Start the user process by simulating a return from an
     * interrupt, implemented by intr_exit (in
     * threads/intr-stubs.S).  Because intr_exit takes all of its
     * arguments on the stack in the form of a `struct intr_frame',
     * we just point the stack pointer (%esp) to our stack frame
     * and jump to it.
     */
    asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
    NOT_REACHED();
}

/*!
 *  Waits for thread TID to die and returns its exit status.  If it was
 *  terminated by the kernel (i.e. killed due to an exception), returns -1.
 *  If TID is invalid or if it was not a child of the calling process, or if
 *  process_wait() has already been successfully called for the given TID,
 *  returns -1 immediately, without waiting.
 */
int process_wait(tid_t child_tid) {
    struct child_process *c
        = thread_get_child_process(thread_current(), child_tid);
    int exit_status = -1;
    if (c) { /* We found a child with this tid */
        /*
         * Wait for the child thread to terminate (the semaphore is upped
         * when the child calls exit_with_status
         */
        sema_down(&c->signal);
        exit_status = c->exit_status;
        /*
         * Child has finished running and waiting again on this thread should
         * return -1, so we delete this struct from list
         */
        list_remove(&c->elem);
        free(c);
    }
    /* We did not find a child with this tid */
    return exit_status;
}

/*!
 *  Free the current process's resources.
 *  This function is called by thread_exit() ifdef USERPROG
 */
void process_exit(void) {
    struct thread *cur = thread_current();
    uint32_t *pd;

    /*
     * Destroy the current process's page directory and switch back
     * to the kernel-only page directory.
     */
    pd = cur->pagedir;
    if (pd != NULL) {
        /* Free all virtual pages held by this thread. */
        struct hash_iterator i;
        hash_first(&i, &cur->supp_page_table);
        while (hash_next(&i)) {
            struct supp_page_table_entry *entry =
                hash_entry(hash_cur(&i), struct supp_page_table_entry, elem);
            /* Mark this frame as free. */
            uint8_t *kpage = pagedir_get_page(pd, entry->page_addr);
            if (kpage) frame_free_page(kpage);
            /* Unmap this virtual page. This is not really necessary because
                the thread is exiting anyway. */
            pagedir_clear_page(pd, entry->page_addr);
        }
        hash_destroy(&cur->supp_page_table, &hash_free_supp_entry);

        /* Correct ordering here is crucial.  We must set
         * cur->pagedir to NULL before switching page directories,
         * so that a timer interrupt can't switch back to the
         * process page directory.  We must activate the base page
         * directory before destroying the process's page
         * directory, or our active page directory will be one
         * that's been freed (and cleared). */
        cur->pagedir = NULL;
        pagedir_activate(NULL);
        pagedir_destroy(pd);
    }
}

/*! Sets up the CPU for running user code in the current thread.
    This function is called on every context switch. */
void process_activate(void) {
    struct thread *t = thread_current();

    /* Activate thread's page tables. */
    pagedir_activate(t->pagedir);

    /* Set thread's kernel stack for use in processing interrupts. */
    tss_update();
}

/*! We load ELF binaries.  The following definitions are taken
    from the ELF specification, [ELF1], more-or-less verbatim.  */

/*! ELF types.  See [ELF1] 1-2. @{ */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;
/*! @} */

/*! For use with ELF types in printf(). @{ */
#define PE32Wx PRIx32   /*!< Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /*!< Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /*!< Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /*!< Print Elf32_Half in hexadecimal. */
/*! @} */

/*! Executable header.  See [ELF1] 1-4 to 1-8.
    This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
};

/*! Program header.  See [ELF1] 2-2 to 2-4.  There are e_phnum of these,
    starting at file offset e_phoff (see [ELF1] 1-6). */
struct Elf32_Phdr {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
};

/*! Values for p_type.  See [ELF1] 2-3. @{ */
#define PT_NULL    0            /*!< Ignore. */
#define PT_LOAD    1            /*!< Loadable segment. */
#define PT_DYNAMIC 2            /*!< Dynamic linking info. */
#define PT_INTERP  3            /*!< Name of dynamic loader. */
#define PT_NOTE    4            /*!< Auxiliary info. */
#define PT_SHLIB   5            /*!< Reserved. */
#define PT_PHDR    6            /*!< Program header table. */
#define PT_STACK   0x6474e551   /*!< Stack segment. */
/*! @} */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. @{ */
#define PF_X 1          /*!< Executable. */
#define PF_W 2          /*!< Writable. */
#define PF_R 4          /*!< Readable. */
/*! @} */

static bool setup_stack(const char *file_name, void **esp);
static bool validate_segment(const struct Elf32_Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
                         uint32_t read_bytes, uint32_t zero_bytes,
                         bool writable);

/*!
 *  Loads an ELF executable from FILE_NAME into the current thread.  Stores the
 *  executable's entry point into *EIP and its initial stack pointer into *ESP.
 *  Returns true if successful, false otherwise.
 */
bool load(const char *file_name, void (**eip) (void), void **esp) {
    struct thread *t = thread_current();
    struct Elf32_Ehdr ehdr;
    struct file *file = NULL;
    off_t file_ofs;
    bool success = false;
    int i;

    /* Allocate and activate page directory. */
    t->pagedir = pagedir_create();
    if (t->pagedir == NULL)
        goto done;
    process_activate();

    /* Open executable file. */
    const char *command_name = get_command_name(file_name);
    struct file_descriptor *file_desp = malloc(sizeof(struct file_descriptor));

    if (command_name == NULL || file_desp == NULL) {
        printf("Malloc failed in load\n");
        goto done;
    }

    file = filesys_open(t->cur_dir, command_name);
    if (file == NULL) {
        printf("load: %s: open failed\n", command_name);
        goto done;
    }

    /* Read and verify executable header. */
    if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr ||
        memcmp(ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2 ||
        ehdr.e_machine != 3 || ehdr.e_version != 1 ||
        ehdr.e_phentsize != sizeof(struct Elf32_Phdr) || ehdr.e_phnum > 1024) {
        printf("load: %s: error loading executable\n", file_name);
        goto done;
    }

    /* Add this file to the list of files opened by this thread */
    file_desp->fd = thread_current()->fd_next++;
    file_desp->file_name = (char *) command_name;
    file_desp->file = file;
    list_push_back(&thread_current()->files, &file_desp->elem);

    /* Deny writing to this executable while it is executing */
    file_deny_write(file_desp->file);

    /* Read program headers. */
    file_ofs = ehdr.e_phoff;
    for (i = 0; i < ehdr.e_phnum; i++) {
        struct Elf32_Phdr phdr;

        if (file_ofs < 0 || file_ofs > file_length(file))
            goto done;
        file_seek(file, file_ofs);

        if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
            goto done;

        file_ofs += sizeof phdr;

        switch (phdr.p_type) {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
            /* Ignore this segment. */
            break;

        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
            goto done;

        case PT_LOAD:
            if (validate_segment(&phdr, file)) {
                bool writable = (phdr.p_flags & PF_W) != 0;
                uint32_t file_page = phdr.p_offset & ~PGMASK;
                uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
                uint32_t page_offset = phdr.p_vaddr & PGMASK;
                uint32_t read_bytes, zero_bytes;
                if (phdr.p_filesz > 0) {
                    /* Normal segment.
                       Read initial part from disk and zero the rest. */
                    read_bytes = page_offset + phdr.p_filesz;
                    zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) -
                                 read_bytes);
                }
                else {
                    /* Entirely zero.
                       Don't read anything from disk. */
                    read_bytes = 0;
                    zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
                }
                if (!load_segment(file, file_page, (void *) mem_page,
                                  read_bytes, zero_bytes, writable))
                    goto done;
            }
            else {
                goto done;
            }
            break;
        }
    }

    /* Set up stack. */
    if (!setup_stack(file_name, esp))
        goto done;

    /* Start address. */
    *eip = (void (*)(void)) ehdr.e_entry;

    success = true;

done:
    /* We arrive here whether the load is successful or not. */
    return success;
}

/* load() helpers. */

/*! Checks whether PHDR describes a valid, loadable segment in
    FILE and returns true if so, false otherwise. */
static bool validate_segment(const struct Elf32_Phdr *phdr, struct file *file) {
    /* p_offset and p_vaddr must have the same page offset. */
    if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
        return false;

    /* p_offset must point within FILE. */
    if (phdr->p_offset > (Elf32_Off) file_length(file))
        return false;

    /* p_memsz must be at least as big as p_filesz. */
    if (phdr->p_memsz < phdr->p_filesz)
        return false;

    /* The segment must not be empty. */
    if (phdr->p_memsz == 0)
        return false;

    /* The virtual memory region must both start and end within the
       user address space range. */
    if (!is_user_vaddr((void *) phdr->p_vaddr))
        return false;
    if (!is_user_vaddr((void *) (phdr->p_vaddr + phdr->p_memsz)))
        return false;

    /* The region cannot "wrap around" across the kernel virtual
       address space. */
    if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
        return false;

    /* Disallow mapping page 0.
       Not only is it a bad idea to map page 0, but if we allowed it then user
       code that passed a null pointer to system calls could quite likely panic
       the kernel by way of null pointer assertions in memcpy(), etc. */
    if (phdr->p_vaddr < PGSIZE)
        return false;

    /* It's okay. */
    return true;
}

/*! Loads a segment starting at offset OFS in FILE at address UPAGE.  In total,
    READ_BYTES + ZERO_BYTES bytes of virtual memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

    The pages initialized by this function must be writable by the user process
    if WRITABLE is true, read-only otherwise.

    Return true if successful, false if a memory allocation error or disk read
    error occurs. */
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
                         uint32_t read_bytes, uint32_t zero_bytes,
                         bool writable) {
    ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT(pg_ofs(upage) == 0);
    ASSERT(ofs % PGSIZE == 0);

    while (read_bytes > 0 || zero_bytes > 0) {
        /* Calculate how to fill this page.
           We will read PAGE_READ_BYTES bytes from FILE
           and zero the final PAGE_ZERO_BYTES bytes. */
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        struct file *new_file = file_reopen(file);
        file_seek(new_file, ofs);
        /* Add an entry to supplemental page table to lazily load later. */
        supp_add_exec_entry(new_file, page_read_bytes, page_zero_bytes,
            writable, upage);
        ofs += page_read_bytes;

        /* Advance. */
        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        upage += PGSIZE;
    }
    return true;
}

/*!
 *  Create a minimal stack by mapping a zeroed page at the top of
 *  user virtual memory.
 */
static bool setup_stack(const char *filename, void **esp) {
    uint8_t *kpage;
    uint8_t *upage = ((uint8_t *) PHYS_BASE) - PGSIZE;
    bool success = false;

    supp_add_stack_entry(upage);
    /* We do not lazily add the first stack page
     * (it is always allocated immediately) */
    kpage = frame_get_page();
    if (kpage != NULL) {
        /* Update the struct */
        update_frame_struct(get_frame_entry(kpage), upage, thread_current());
        success = install_page(upage, kpage, true);
        if (success) {
            /* Calculate the total size/length of the arguments */
            int char_count = 0;
            int word_count = 0;
            int i = 0;
            bool last_space = true; // true if last char was a space
            while (filename[i] != '\0') {
                if (filename[i] != ' ') {
                    if (last_space) {
                        word_count++;
                        last_space = false;
                    }
                    char_count++;
                }
                else {
                    last_space = true;
                }
                i++;
            }

            /* To accommodate a program, we require space for:
             * 1. All arguments - char_count + word_count
             * 2. Word alignment - WORD_SIZE - 1 < WORD_SIZE
             * 3. Pointer to each arg - word_count * sizeof(char *)
             * 4. Pointer to the pointer to argv[0] - sizeof(char *)
             * 5. argc - sizeof(int)
             * 6. Return address / esp / extra padding - WORD_SIZE
             */
            int required_stack_size = (char_count + word_count)
                                      + WORD_SIZE
                                      + word_count * sizeof(char *)
                                      + sizeof(char *)
                                      + sizeof(int)
                                      + WORD_SIZE;
            /*
             * If the required stack size is more than one page, we simply
             * do not accommodate this program and fail to load.
             */
            if (required_stack_size > PGSIZE) {
                return false;
            }

            /* Add arguments to the stack */
            /* 1. Add each argument followed by a null char to the stack */
            *esp = PHYS_BASE - (char_count + word_count);
            char * args = *esp;
            i = 0;
            int j = 0;
            while (filename[i] != '\0') {
                /* Add every non-space char in filename to the stack */
                if (filename[i] != ' ') {
                    args[j] = filename[i];
                    j++;
                }
                /* Add a null terminator for every nonconsecutive space
                 * in filename */
                else if (j != 0 && args[j - 1] != '\0') {
                    args[j] = '\0';
                    j++;
                }
                i++;
            }
            /* Add the last null terminator if it hasn't been added yet */
            args[word_count + char_count - 1] = '\0';

            /*
             * 2. Word align esp by rounding down to a multiple of 4
             * since word-aligned accesses are faster than unaligned accesses
             * It is important that *esp be cast to an unsigned int as opposed
             * to a regular int (otherwise the mod turns out to be negative
             * since esp can be "negative")
             */
            *esp = *esp - ((uint32_t) *esp % WORD_SIZE);

            /*
             * 3. Add the pointers to the args (the last pointer is NULL
             * so we simply don't add anything there - leaving it as 0)
             */
            char ** arg_ptrs = (char **) (*esp - (word_count + 1) * WORD_SIZE);
            char * temp = args;
            bool last_null = true; // true if last char was a NULL
            i = 0;
            while (temp < (char *) PHYS_BASE) {
                if (last_null) {
                    arg_ptrs[i] = temp;
                    i++;
                    last_null = false;
                }
                else if (*temp == '\0') {
                    last_null = true;
                }
                temp++;
            }
            *esp = (char *) arg_ptrs;
            *esp -= WORD_SIZE;

            /* 4. Add the pointer to the pointer to argv[0] */
            char *argv = (char *) (*esp + WORD_SIZE);
            *((char **) *esp) = (char *) argv;
            *esp -= WORD_SIZE;

            /* 5. Add argc */
            * (int *) *esp = word_count;

            /* esp now points to the start (bottom) of the stack */
            *esp -= WORD_SIZE;
        }
        else
            frame_free_page(kpage);
    }
    return success;
}
