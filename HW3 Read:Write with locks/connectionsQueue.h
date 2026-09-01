#include "segel.h"

typedef struct t_connectionsQueue *queuePtr;
typedef struct t_pendingConnection *connectionPtr;

struct t_pendingConnection {
    int fd;
    struct timeval arrival_time;
    connectionPtr nextConnection;
};

struct t_connectionsQueue {
    //we strive to give main thread push pref, so it is free as possible to handle requests.
    connectionPtr head;
    connectionPtr tail;
    int waitingCount;
    int inProgressCount;
    int queueCapacity;
    pthread_mutex_t lock;
    pthread_cond_t not_empty, not_full;
};

// functions
queuePtr createQueue(int size);

connectionPtr createConnection(int fd, struct timeval arrival_time);

void pushConnection(queuePtr q, int fd, struct timeval arrival_time);

connectionPtr popConnection(queuePtr q);

void decConnectionCounter(queuePtr q);
