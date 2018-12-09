#include "threads/synch.h"

/*! Read-write lock. */
struct rw_lock {
    struct lock lock;          /* Read/write lock */
    int num_readers;           /* Number of readers currently holding lock */
    int num_readers_waiting;   /* Number of readers waiting for the lock */
    int num_writers_waiting;   /* Number of writers waiting for the lock */
    int num_readers_permitted; /* Number of readers to let read (writer should
                                * not acquire the lock until this many readers
                                * have acquired the lock */
    struct condition readers;  /* Contains the list of waiting reader threads */
    struct condition writers;  /* Contains the list of waiting writer threads */
};

void rw_lock_init(struct rw_lock *);
void rw_read_acquire(struct rw_lock *);
void rw_write_acquire(struct rw_lock *);
void rw_read_release(struct rw_lock *);
void rw_write_release(struct rw_lock *);