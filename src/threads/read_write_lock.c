#include "threads/read_write_lock.h"

void rw_lock_init(struct rw_lock *rw_lock) {
    lock_init(&rw_lock->lock);
    int num_readers = 0;
    int num_readers_waiting = 0;
    int num_writers_waiting = 0;
    int num_readers_permitted = 0;
    cond_init(&rw_lock->readers);
    cond_init(&rw_lock->writers);
}

void rw_read_acquire(struct rw_lock *rw_lock) {
    rw_lock->num_readers_waiting++;
    lock_acquire(&rw_lock->lock);
    while (num_writers_waiting > 0 && num_readers_permitted == 0) {
        cond_wait(&readers, &rw_lock->lock);
    }
    rw_lock->num_readers_waiting--;
    rw_lock->num_readers_permitted = rw_lock->num_readers_permitted != 0
        ? rw_lock->num_readers_permitted - 1 : 0;
    rw_lock->num_readers++;
    lock_release(&rw_lock->lock);
}

void rw_read_release(struct rw_lock *rw_lock) {
    lock_acquire(&rw_lock->lock);
    rw_lock->num_readers--;
    if (rw_lock->num_readers == 0) {
        ASSERT(rw_lock->num_readers_permitted == 0);
        cond_signal(&writers, &rw_lock->lock);
    }
    lock_release(&rw_lock->lock);
}

void rw_write_acquire(struct rw_lock *rw_lock) {
    rw_lock->num_writers_waiting++;
    lock_acquire(&rw_lock->lock);
    while (num_readers > 0 || num_readers_permitted > 0) {
        cond_wait(&writers, &rw_lock->lock);
    }
    rw_lock->num_writers_waiting--;
}

void rw_write_release(struct rw_lock *rw_lock) {
    cond_broadcast(&readers, &rw_lock->lock);
    rw_lock->num_readers_permitted = rw_lock->num_readers_waiting;
    lock_release(&rw_lock->lock);
}