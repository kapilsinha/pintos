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

/*!
 *  Adds a supplemental page table entry for when an executable is loaded
 */
void supp_add_exec_entry(struct file *f, uint32_t page_data_bytes,
    uint32_t page_zero_bytes, void *page_addr) {
    struct thread * t = thread_current();
    struct supp_page_table_entry * exec_entry
        = malloc(sizeof(struct supp_page_table_entry));
    if (exec_entry == NULL) {
        PANIC("Malloc in add_exec_entry failed");
    }
    if (! is_user_vaddr(page_addr)) {
        PANIC("Page address is not in user space in add_exec_entry");
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
}
