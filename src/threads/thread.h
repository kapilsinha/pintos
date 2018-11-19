/*! \file thread.h
 *
 * Declarations for the kernel threading functionality in PintOS.
 */

#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include <threads/synch.h>
#include <filesys/file.h>

/*! States in a thread's life cycle. */
enum thread_status {
    THREAD_RUNNING,     /*!< Running thread. */
    THREAD_READY,       /*!< Not running but ready to run. */
    THREAD_BLOCKED,     /*!< Waiting for an event to trigger. */
    THREAD_DYING        /*!< About to be destroyed. */
};

/*! Thread identifier type.
    You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /*!< Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /*!< Lowest priority. */
#define PRI_DEFAULT 31                  /*!< Default priority. */
#define PRI_MAX 63                      /*!< Highest priority. */

/*! A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

\verbatim
        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
             |               tid               |
        0 kB +---------------------------------+
\endverbatim

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion.

   The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list.
*/

/* Struct for child processes of this thread.
 * Used for implementing wait and exec in userprog
 * Sets up synchronization between parent and child threads until the
 * child loads its file
 */
struct child_process {
    struct thread *child;    /* Pointer to the child thread. */
    /* Child_tid is necessary in addition to the child pointer because
     * the child tid must persist after the child may have exited, in
     * order to properly wait */
    int child_tid;           /* Tid of the child. */
    struct semaphore signal; /* Semaphore to signal child has executed. */
    int exit_status;         /* Exit status of the child. */
    /* Semaphore to force thread to yield to its child so it can load */
    struct semaphore load_sema;
    /* Semaphore to force thread to yield to its parent after loading */
    struct semaphore parent_load_sema;
    /* true if thread loaded succeeded, false otherwise */
    bool is_load_successful;
    struct list_elem elem; /* Linked list element */
};

/* Struct for file descriptors of this thread. */
struct file_descriptor {
    int fd;                /* Integer file descriptor for this file. */
    char *file_name;       /* Name of file. Used to check if file is open. */
    struct file *file;     /* Pointer to the file struct for this file. */
    struct list_elem elem; /* Linked list element. */
};

struct thread {
    /*! Owned by thread.c. */
    /**@{*/
    tid_t tid;                      /*!< Thread identifier. */
    enum thread_status status;      /*!< Thread state. */
    char name[16];                  /*!< Name (for debugging purposes). */
    uint8_t *stack;                 /*!< Saved stack pointer. */
    int priority;                   /*!< Priority. */
    struct list_elem allelem;       /*!< List element for all threads list. */
    /**@}*/

    /*! Used for implementing waiting for userprog assignment. */
    /**@{*/
    struct list children;    /*!< List of this thread's immediate children's
                                  child_process structs. */
    struct thread *parent;   /*!< Pointer to the parent thread. */
    /**@}*/

    /*! Used for implementing the file system for the userprog assignment. */
    /**@{*/
    struct list files;       /*!< List of file descriptors for files opened
                                  by this thread. */
    int fd_next;             /*!< fd of the next opened file by this thread */
    /**@}*/

    /*! Shared between thread.c and synch.c. */
    /**@{*/
    struct list_elem elem;   /*!< List element. */
    /**@}*/

#ifdef USERPROG
    /*! Owned by userprog/process.c. */
    /**@{*/
    uint32_t *pagedir;       /*!< Page directory. */
    /**@{*/
#endif

    /*! Owned by thread.c. */
    /**@{*/
    unsigned magic;          /* Detects stack overflow. */
    /**@}*/
};

/*! If false (default), use round-robin scheduler.
    If true, use multi-level feedback queue scheduler.
    Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);
void thread_print_child_processes(struct thread *parent);

typedef void thread_func(void *aux);

/* Creates a thread and updates the child_process structs in the parent
 * and file_descriptor structs in the current thread */
tid_t child_thread_create
    (const char *name, int priority, thread_func *, void *);
tid_t thread_create(const char *name, int priority, thread_func *, void *);

void thread_block(void);
void thread_unblock(struct thread *);

struct thread *thread_current (void);
tid_t thread_tid(void);
const char *thread_name(void);

void thread_exit(void) NO_RETURN;
void thread_yield(void);

/*! Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func(struct thread *t, void *aux);

void thread_foreach(thread_action_func *, void *);

struct child_process *thread_get_child_process
    (struct thread *parent, tid_t child_tid);

int thread_get_priority(void);
void thread_set_priority(int);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

#endif /* threads/thread.h */
