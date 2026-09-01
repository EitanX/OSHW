#ifndef RWLOCK_H
#define RWLOCK_H

#include <pthread.h>
#include "segel.h"

void readers_writers_init(void);

void reader_lock(void);

void reader_unlock(void);

void writer_lock(void);

void writer_unlock(void);

#endif /* RWLOCK_H */
