#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <debug.h>
#include <string.h>
#include <stdio.h>
#include "hash.h"
#include "filesys/file.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "threads/thread.h"
#include "userprog/syscall.h"
#include "frame.h"

/*!
 *  Type of page source
 */
enum page_source_type {
    PAGE_SOURCE_EXECUTABLE, /* Virtual page loaded in an executable */
    PAGE_SOURCE_STACK,      /* Virtual page represents a program stack */
    PAGE_SOURCE_MMAP        /* Virtual page loaded in a memory-mapped file */
};

/*!
 *  Struct containing the information to write or load from a backing file
 */
struct backing_file {
    struct file *file;        /* file struct for the backing file */
    uint32_t page_data_bytes; /* number of bytes to read/write from/to page */
    uint32_t page_zero_bytes; /* number of bytes to zero out at the end
                                 when reading a file into a page */
    bool writable;            /* true if read/write, False if read-only */
};

/*!
 *  Struct for supplemental page table of this thread.
 *  Hash table maps virtual page addresses to the following struct which has
 *  supplemental information about the page.
 */
struct supp_page_table_entry {
    enum page_source_type type; // Type of page source (what the page contains)
    void *page_addr;         // Virtual page address
    bool save_loc;           // 1 to save to swap, 0 to save to backing file
    bool load_loc;           // 1 to load from swap, 0 to load from backing file
    int eviction_status;     // 1 if page is evicted, 0 if it is in memory,
                             // 2 if in the process of 
    struct lock evict_lock;  // If in process of evicting, acquire this lock
    size_t swap_slot;        // Swap slot that contains this page if load_loc=1
    struct backing_file bf;  // Data to read/write to backing file
    struct hash_elem elem;   // Element for hash table
};

/*!
 *  Struct for mmap table of this thread
 *  Hash table maps mapid to the following struct which has additional
 *  information about the mmap file
 */
struct mmap_table_entry {
    mapid_t mapping;         // Mapping ID of this mmap entry
    void *page_addr;         /* Virtual page address */
    off_t file_size;         /* Size of the backing file */
    int fd;                  // File descriptor of this mmap file
    struct file *file;       /* File struct for the backing file */
    struct hash_elem elem;   // Element for hash table
};

/* Hash hash and comparison functions */
unsigned vaddr_hash (const struct hash_elem *v_, void *aux UNUSED);
bool vaddr_less (const struct hash_elem *a_, const struct hash_elem *b_,
    void *aux UNUSED);
void hash_free_supp_entry(struct hash_elem *e, void *aux UNUSED);

unsigned mmap_hash (const struct hash_elem *v_, void *aux UNUSED);
bool mmap_less (const struct hash_elem *a_, const struct hash_elem *b_,
    void *aux UNUSED);
void hash_free_mmap_entry(struct hash_elem *e, void *aux UNUSED);

/* Finds the supplemental page table entry for a user page */
struct supp_page_table_entry *find_entry(void *upage, struct thread *t);
void print_hash_table(struct hash *h, int bucket_idx);
bool install_page(void *upage, void *kpage, bool writable);
struct supp_page_table_entry *find_entry(void *upage, struct thread *t);

/* Determines whether we grow the stack depending on where vaddr is located
 * in relation to the stack pointer f->esp */
bool valid_stack_growth(void *vaddr, struct intr_frame *f);

/* Add a supplemental page table entry for loading an executable */
void supp_add_exec_entry(struct file *f, uint32_t page_data_bytes,
    uint32_t page_zero_bytes, bool writable, void *page_addr);
/* Add a supplemental page table entry for a stack page */
void supp_add_stack_entry(void *page_addr);
/* Add a supplemental page entry for loading an mmap file */
void supp_add_mmap_entry(struct file *f, uint32_t page_data_bytes,
    uint32_t page_zero_bytes, bool writable, void *page_addr);

/* Handles a generic page fault */
bool handle_page_fault(void *page_addr, struct intr_frame *f);
/* Load an executable source page to physical memory */
bool load_exec(struct supp_page_table_entry * exec_entry);
/* Load a stack source page to physical memory */
bool load_stack(struct supp_page_table_entry * stack_entry);
/* Load a mmap source page to physical memory */
bool load_mmap(struct supp_page_table_entry * mmap_entry);
/* Loads a file from swap back into memory */
bool load_swap(struct supp_page_table_entry *swap_entry);

#endif /* vm/page.h */
