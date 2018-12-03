#include <debug.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/*! Prints the call stack, that is, a list of addresses, one in
    each of the functions we are nested within.  gdb or addr2line
    may be applied to kernel.o to translate these into file names,
    line numbers, and function names.  */
void debug_backtrace(void) {
    static bool explained;
    void **frame;
  
    printf("Call stack: %p", __builtin_return_address (0));
    for (frame = __builtin_frame_address(1);
         (uintptr_t) frame >= 0x1000 && frame[0] != NULL;
         frame = frame[0]) {
        printf (" %p", frame[1]);
    }
    printf (".\n");

    if (!explained) {
        explained = true;
        printf ("The `backtrace' program can make call stacks useful.\n"
                "Read \"Backtraces\" in the \"Debugging Tools\" chapter\n"
                "of the Pintos documentation for more information.\n");
    }
}

/*
 * Print word dump of a buffer starting at addr and of size length bytes
 */
void debug_word_dump(void *addr, int length) {
    uint32_t *curr = (uint32_t *) addr;
    uint32_t *end = (uint32_t *) ((char *) addr + length);
    printf("Word dump:\n");
    printf("Start: %#04x\n", (unsigned int) curr);
    printf("End: %#04x\n", (unsigned int) end);
    while (curr < end) {
        printf("Address: %#04x -- Value: %#04x\n", (unsigned int) curr, *curr);
        curr++;
    }
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
