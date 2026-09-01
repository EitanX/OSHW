#include "rwlock.h"

int readers_inside = 0;
int writers_inside = 0;
int writers_waiting = 0;

pthread_mutex_t global_lock;
pthread_cond_t read_allowed;
pthread_cond_t write_allowed;

void readers_writers_init(void) {
    readers_inside = 0;
    writers_inside = 0;
    writers_waiting = 0;

    if (pthread_mutex_init(&global_lock, NULL) != 0)
        unix_error("pthread_mutex_init failed");
    if (pthread_cond_init(&read_allowed, NULL) != 0)
        unix_error("pthread_cond_init read_allowed failed");
    if (pthread_cond_init(&write_allowed, NULL) != 0)
        unix_error("pthread_cond_init write_allowed failed");
}

void reader_lock(void) {
    pthread_mutex_lock(&global_lock);
    while (writers_inside > 0 || writers_waiting > 0)
        pthread_cond_wait(&read_allowed, &global_lock);
    readers_inside++;
    pthread_mutex_unlock(&global_lock);
}

void reader_unlock(void) {
    pthread_mutex_lock(&global_lock);
    readers_inside--;
    if (readers_inside == 0)
        pthread_cond_signal(&write_allowed);
    pthread_mutex_unlock(&global_lock);
}

void writer_lock(void) {
    pthread_mutex_lock(&global_lock);
    writers_waiting++;
    while (writers_inside > 0 || readers_inside > 0)
        pthread_cond_wait(&write_allowed, &global_lock);
    writers_waiting--;
    writers_inside++;
    pthread_mutex_unlock(&global_lock);
}

void writer_unlock(void) {
    pthread_mutex_lock(&global_lock);
    writers_inside--;
    if (writers_inside == 0) {
        pthread_cond_signal(&write_allowed);
        pthread_cond_broadcast(&read_allowed);
    }
    pthread_mutex_unlock(&global_lock);
}
