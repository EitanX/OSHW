#include "segel.h"
#include "request.h"
#include "log.h"
#include "connectionsQueue.h"

//
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

queuePtr queue;

threads_stats stats;

server_log s_log;


void *worker(void *arg);


// Parses command-line arguments
void getargs(int *port, int *threads_num, int *queue_size, int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <port> <threads_num> <queue_size>\n", argv[0]);
        exit(1);
    }
    *port = atoi(argv[1]);
    *threads_num = atoi(argv[2]);
    *queue_size = atoi(argv[3]);

}
// TODO: HW3 — Initialize thread pool and request queue
// This server currently handles all requests in the main thread.
// You must implement a thread pool (fixed number of worker threads)
// that process requests from a synchronized queue.

int main(int argc, char *argv[]) {
    // Create the global server log
    s_log = create_log();

    int listenfd, connfd, port, clientlen, threads_num, queue_size;
    struct sockaddr_in clientaddr;

    getargs(&port, &threads_num, &queue_size, argc, argv);


    //create queue
    queue = createQueue(queue_size);
    if (!queue) {
        unix_error("malloc failed");
    }

    stats = malloc(threads_num * sizeof(struct Threads_stats));
    if (!stats) {
        //TODO: maybe write destroyQueue for pthread_mutex_destroy
        free(queue);
        unix_error("malloc failed");
    }
    for (int i = 0; i < threads_num; ++i) {
        stats[i].stat_req = 0;
        stats[i].dynm_req = 0;
        stats[i].post_req = 0;
        stats[i].total_req = 0;

    }
    pthread_t *workers = malloc(threads_num * sizeof(pthread_t));
    if (!workers) {
        free(stats);
        free(queue);
        unix_error("malloc failed");
    }

    for (int i = 0; i < threads_num; i++) {
        stats[i].id = i + 1;
        int rc = pthread_create(&workers[i], NULL, worker, (void *) (long) i);
        if (rc != 0) {
            //TODO: maybe pthread_cancel the thread that already created

            free(stats);
            free(queue);
            free(workers);
            posix_error(rc, "pthread_create failed");
        }
    }


    listenfd = Open_listenfd(port);
    while (1) {
        clientlen = sizeof(clientaddr);
        pthread_mutex_lock(&queue->lock);

        while (queue->waitingCount + queue->inProgressCount >= queue->queueCapacity) {
            pthread_cond_wait(&queue->not_full, &queue->lock);
        }

        pthread_mutex_unlock(&queue->lock);

        connfd = Accept(listenfd, (SA *) &clientaddr, (socklen_t *) &clientlen);

        // TODO: HW3 — Record the request arrival time here
        struct timeval arrival;
        gettimeofday(&arrival, NULL);


        pushConnection(queue, connfd, arrival);
    }

    // Clean up the server log before exiting
    free(queue);
    free(stats);
    free(workers);
    destroy_log(s_log);
    return 0;

    // TODO: HW3 — Add cleanup code for thread pool and queue
}

void *worker(void *arg) {
    int my_idx = (int)(long)arg;
    threads_stats my_stats = &stats[my_idx];

    while (1) {
        connectionPtr conn = popConnection(queue);

        int fd = conn->fd;
        struct timeval arrival = conn->arrival_time;
        free(conn);

        struct timeval now, dispatch;
        gettimeofday(&now, NULL);

        dispatch.tv_sec  = now.tv_sec  - arrival.tv_sec;
        dispatch.tv_usec = now.tv_usec - arrival.tv_usec;
        if (dispatch.tv_usec < 0) {
            dispatch.tv_sec  -= 1;
            dispatch.tv_usec += 1000000;
        }

        requestHandle(fd, arrival, dispatch, my_stats, s_log);
        Close(fd);

        decConnectionCounter(queue);
    }
    return NULL;
}

//
//void *worker(void *arg) {
//    int my_idx = (int) (long) arg;
//    threads_stats my_stats = &stats[my_idx];
//
//    while (1) {
//        connectionPtr conn = popConnection(queue);
//
//        int fd = conn->fd;
//        struct timeval arrival = conn->arrival_time;
//        free(conn);
//
//        struct timeval dispatch;
//        gettimeofday(&dispatch, NULL);
//
//        requestHandle(fd, arrival, dispatch, my_stats, s_log);
//        Close(fd);
//
//        decConnectionCounter(queue);
//    }
//    return NULL;
//}
//
