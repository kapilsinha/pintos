#include "page.h"

/* Implementation of supplemental page table. */

/*
 * The supplemental page table supplements the page table with additional data
 * about each page. It is needed because of the limitations imposed by the
 * processor's page table format.
 */

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
 *  Adds a supplemental page table entry for when an executable is loaded
 */
void supp_add_exec_entry(struct file *f, uint32_t page_data_bytes,
    uint32_t page_zero_bytes, void *page_addr) {
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
    struct hash_elem * elem
        = hash_insert(&t->supp_page_table, &stack_entry->elem);
    /* This vaddr should not be present in the hash table;
     * if it is, there may be bugs in the stack growth */
    if (elem != NULL) {
        PANIC("This stack vaddr was already in the hash table");
    }
}

/*! Load an executable source page to physical memory
 *  This function should be called upon a page fault to bring
 *  into memory a certain page.
 */
void load_exec(void *page_addr UNUSED) {
    return;
}
