#include "page.h"

/* Implementation of supplemental page table. */

/*
 * The supplemental page table supplements the page table with additional data
 * about each page. It is needed because of the limitations imposed by the
 * processor's page table format.
 */

#define list_elem_to_hash_elem(LIST_ELEM)                       \
        list_entry(LIST_ELEM, struct hash_elem, list_elem)


/*! Hash function for the supplemental page table. */
unsigned vaddr_hash (const struct hash_elem *v_, void *aux UNUSED) {
    const struct supp_page_table_entry *v
        = hash_entry(v_, struct supp_page_table_entry, elem);
    return hash_bytes(&v->page_addr, sizeof(v->page_addr));
}

/*! Returns true if page a precedes page b. */
bool vaddr_less (const struct hash_elem *a_, const struct hash_elem *b_,
    void *aux UNUSED) {
    const struct supp_page_table_entry *a
        = hash_entry (a_, struct supp_page_table_entry, elem);
    const struct supp_page_table_entry *b
        = hash_entry (b_, struct supp_page_table_entry, elem);
    return a->page_addr < b->page_addr;
}

/* Helper functions */

/*! Finds the corresponding supplemental page table entry for a given user page
 *  and thread. If none are found, returns NULL.
 */
struct supp_page_table_entry *find_entry(void *upage, struct thread *t) {
    struct supp_page_table_entry to_find;
    to_find.page_addr = upage;
    struct hash_elem *e = hash_find(&t->supp_page_table, &to_find.elem);
    return e != NULL ? hash_entry(e, struct supp_page_table_entry, elem) : NULL;
}

/* Prints out everything in the hash table. */
void print_hash_table(struct hash *h, int bucket_idx) {
    struct list *bucket = &h->buckets[bucket_idx];
    for (struct list_elem *i = list_begin(bucket); i != list_end(bucket); i = list_next(i)) {
        struct hash_elem *hi = list_elem_to_hash_elem(i);
        struct supp_page_table_entry *e = hash_entry (hi, struct supp_page_table_entry, elem);
        printf("%p\n", e->page_addr);
    }
}

/*! Adds a mapping from user virtual address UPAGE to kernel
    virtual address KPAGE to the page table.
    If WRITABLE is true, the user process may modify the page;
    otherwise, it is read-only.
    UPAGE must not already be mapped.
    KPAGE should probably be a page obtained from the user pool
    with frame_get_page().
    Returns true on success, false if UPAGE is already mapped or
    if memory allocation fails. */
bool install_page(void *upage, void *kpage, bool writable) {
    struct thread *t = thread_current();

    /* Verify that there's not already a page at that virtual
       address, then map our page there. */
    return (pagedir_get_page(t->pagedir, upage) == NULL &&
            pagedir_set_page(t->pagedir, upage, kpage, writable));
}

/*!
 *  Determines whether to grow the stack depending on vaddr and
 *  the stack pointer (f->esp).
 *  The stack is grown if the faulting address is either 4 or 32
 *  bytes under the stack pointer (for PUSH or PUSHA instructions)
 *  or if the faulting address is above the stack pointer.
 */
bool valid_stack_growth(void *vaddr, struct intr_frame *f) {
    return is_user_vaddr(vaddr) && (vaddr == f->esp - 4
           || vaddr == f->esp - 32 || vaddr > f->esp);
}

/*!
 *  Adds a supplemental page table entry for when an executable is loaded
 */
void supp_add_exec_entry(struct file *f, uint32_t page_data_bytes,
    uint32_t page_zero_bytes, bool writable, void *page_addr) {
    struct thread * t = thread_current();
    if (! is_user_vaddr(page_addr)) {
        PANIC("Page address is not in user space in add_exec_entry");
    }
    struct supp_page_table_entry * exec_entry
        = malloc(sizeof(struct supp_page_table_entry));
    if (exec_entry == NULL) {
        PANIC("Malloc in add_exec_entry failed");
    }
    exec_entry->type = PAGE_SOURCE_EXECUTABLE;
    exec_entry->page_addr = page_addr;
    exec_entry->save_loc = 1;
    exec_entry->load_loc = 0;
    exec_entry->eviction_status = 1;
    exec_entry->bf.file = f;
    exec_entry->bf.page_data_bytes = page_data_bytes;
    exec_entry->bf.page_zero_bytes = page_zero_bytes;
    exec_entry->bf.writable = writable;
    struct hash_elem * elem
        = hash_insert(&t->supp_page_table, &exec_entry->elem);
    /* This vaddr should not be present in the hash table;
     * if it is, there may be bugs in the loading of executables */
    if (elem != NULL) {
        PANIC("This exec vaddr was already in the hash table");
    }
}

/*!
 *  Adds a supplemental page table entry for when a stack page is created
 */
void supp_add_stack_entry(void *page_addr) {
    struct thread * t = thread_current();
    if (! is_user_vaddr(page_addr)) {
        PANIC("Page address is not in user space in add_exec_entry");
    }
    struct supp_page_table_entry * stack_entry
        = malloc(sizeof(struct supp_page_table_entry));
    if (stack_entry == NULL) {
        PANIC("Malloc in add_stack_entry failed");
    }
    stack_entry->type = PAGE_SOURCE_STACK;
    stack_entry->page_addr = page_addr;
    stack_entry->save_loc = 1;
    stack_entry->eviction_status = 0;
    stack_entry->bf.writable = true;
    struct hash_elem * elem
        = hash_insert(&t->supp_page_table, &stack_entry->elem);
    /* This vaddr should not be present in the hash table;
     * if it is, there may be bugs in the stack growth */
    if (elem != NULL) {
        PANIC("This stack vaddr was already in the hash table");
    }
}

/*!
 *  Handles a generic page fault by determining the page source type
 *  and then calling the appropriate specific function
 */
bool handle_page_fault(void *page_addr, struct intr_frame *f) {
    struct thread *t = thread_current();
    uint8_t *upage = pg_round_down(page_addr);
    struct supp_page_table_entry *entry = find_entry(upage, t);
    /*
    printf("Hash table size: %d\n", hash_size(&t->supp_page_table));
    printf("Looking for address : %p\n", page_addr);
    for (int i = 0; i < t->supp_page_table.bucket_cnt; i++) {
        print_hash_table(&t->supp_page_table, i);
    }
    */

    if (! entry && valid_stack_growth(page_addr, f)) {
         supp_add_stack_entry(upage);
         return load_stack(find_entry(upage, t));
    }

    if (entry == NULL) {
        return false;
    }
    if (entry->type == PAGE_SOURCE_EXECUTABLE) {
        return load_exec(entry);
    }
    if (entry->type == PAGE_SOURCE_STACK) {
        return load_stack(entry);
    }
    PANIC("Haven't handled this kind of page fault yet");
}

/*! Load an executable source page to physical memory
 *  This function should be called upon a page fault to bring
 *  into memory a certain page.
 *  Returns true if executable loads properly, else false
 */
bool load_exec(struct supp_page_table_entry *exec_entry) {
    struct file * file = exec_entry->bf.file;
    size_t page_read_bytes = exec_entry->bf.page_data_bytes;
    size_t page_zero_bytes = exec_entry->bf.page_zero_bytes;
    bool writable = exec_entry->bf.writable;
    uint8_t *upage = exec_entry->page_addr;

    // Get a page of memory.
    uint8_t *kpage = frame_get_page();
    if (kpage == NULL)
        return false;

    // Load this page.
    if (file_read(file, kpage, page_read_bytes) != (int) page_read_bytes) {
        frame_free_page(kpage);
        return false;
    }

    memset(kpage + page_read_bytes, 0, page_zero_bytes);

    // Add the page to the process's address space.
    if (!install_page(upage, kpage, writable)) {
        frame_free_page(kpage);
        return false;
    }
    return true;
}

/*! Load a stack page to physical memory
 *  This function should be called upon a page fault to bring
 *  in a certain stack page into memory.
 *  Returns true if stack page load properly, else false
 */
bool load_stack(struct supp_page_table_entry *stack_entry) {
    uint8_t *upage = stack_entry->page_addr;
    bool writable = stack_entry->bf.writable;

    // Get a page of memory.
    uint8_t *kpage = frame_get_page();
    if (kpage == NULL)
        return false;

    memset(kpage, 0, PGSIZE);

    // Add the page to the process's address space.
    if (!install_page(upage, kpage, writable)) {
        frame_free_page(kpage);
        return false;
    }
    return true;
}
