#include "connectionsQueue.h"

queuePtr createQueue(int maxSize) {
    queuePtr q = malloc(sizeof(struct t_connectionsQueue));
    if (q == NULL) return NULL;

    q->head = NULL;
    q->tail = NULL;
    q->waitingCount = 0;
    q->inProgressCount = 0;
    q->queueCapacity = maxSize;

    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);

    return q;
}

connectionPtr createConnection(int fd, struct timeval arrival_time) {
    connectionPtr conn = malloc(sizeof(struct t_pendingConnection));
    if (conn == NULL) return NULL;

    conn->fd = fd;
    conn->arrival_time = arrival_time;
    conn->nextConnection = NULL;
    return conn;
}


void pushConnection(queuePtr q, int fd, struct timeval arrival_time) {
//    pthread_mutex_lock(&q->lock);
    //while we are full, main thread should wait for not_full condition signal
//    while (q->waitingCount + q->inProgressCount >= q->queueCapacity) {
//        pthread_cond_wait(&q->not_full, &q->lock);
//    }

    connectionPtr newConnection = createConnection(fd, arrival_time);
    if (q->head == NULL) {
        q->head = newConnection;
    } else {
        q->tail->nextConnection = newConnection;
    }
    q->tail = newConnection;
    q->waitingCount++;

    pthread_cond_signal(&q->not_empty); //wake up slave thread
//    pthread_mutex_unlock(&q->lock);
}

connectionPtr popConnection(queuePtr q) {
    pthread_mutex_lock(&q->lock);
    //while we are empty, slave thread should wait for not_empty condition signal
    while (q->waitingCount == 0) {
        pthread_cond_wait(&q->not_empty, &q->lock);
    }

    connectionPtr currentConnection = q->head;
    q->head = q->head->nextConnection;
    if (q->head == NULL)
        q->tail = NULL;
    q->waitingCount--;
    q->inProgressCount++;

    pthread_mutex_unlock(&q->lock);
    return currentConnection;
}

void decConnectionCounter(queuePtr q) {
    pthread_mutex_lock(&q->lock);
    //if we are trying to decrease it means we had a connection in our hands so we can dec the val
    q->inProgressCount--;

    pthread_cond_signal(&q->not_full); //wake up main thread
    pthread_mutex_unlock(&q->lock);
    return;
}
