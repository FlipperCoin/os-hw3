#include "segel.h"
#include "request.h"

// 
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

// HW3: Parse the new arguments too
void getargs(int *port, int* threads, int* queue_size, char** schedalg, int argc, char *argv[])
{
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <portnum> <threads> <queue_size> <schedalg>\n", argv[0]);
        exit(1);
    }
    // TODO: check piazza for valid input
    *port = atoi(argv[1]);
    *threads = atoi(argv[2]);
    *queue_size = atoi(argv[3]);
    *schedalg = argv[4];
}

#define DEBUG 0

typedef struct {
    int fd;
    int is_dropped;
    struct timeval arrival_time; 
} queue_entry_t;

queue_entry_t *queue;
pthread_mutex_t queue_m;
pthread_cond_t queue_empty_cond;
pthread_cond_t queue_full_cond;
int queue_count; // number of items waiting in queue
int queue_size; // total size of queue
int queue_tail;
int queue_head;
int threads_working;

void* workerThread(void* args)
{
    int thread_id = *((int*)args);
    free(args);
    int thread_count = 0;
    int first_run = 1;
    int static_count = 0;
    int dynamic_count = 0;
    while (1) {
        pthread_mutex_lock(&queue_m);   
        
        if (!first_run) {
            threads_working--;
            // if not full anymore
            if (queue_count + threads_working == queue_size-1) {
                if (DEBUG) printf("DEBUG %lu worker thread %d: queue not full, signaling...\n", time(NULL), thread_id);
                pthread_cond_signal(&queue_full_cond);
            }
        }
        first_run = 0;
        
        if (queue_count < 1 && DEBUG) printf("DEBUG %lu worker thread %d: queue empty, waiting...\n", time(NULL), thread_id);
        while (queue_count < 1) {
            pthread_cond_wait(&queue_empty_cond, &queue_m);
        }

        if (DEBUG) printf("DEBUG %lu worker thread %d: handling request in queue: %d.\n", time(NULL), thread_id, queue_head);
        
        queue_entry_t entry = queue[queue_head];
        int clifd = entry.fd;
        queue_head = (queue_head + 1) % queue_size;
        queue_count--;
        threads_working++;

        pthread_mutex_unlock(&queue_m);

        struct timeval dispatch_time;
        gettimeofday(&dispatch_time,NULL); 
        struct timeval interval;
        timersub(&dispatch_time,&entry.arrival_time,&interval);

        thread_count++;

        stats_t stats;
        stats.arrival_time = entry.arrival_time;
        stats.interval = interval;
        stats.thread_id = thread_id;
        stats.thread_count = thread_count;
        stats.static_count = &static_count;
        stats.dynamic_count = &dynamic_count;

        requestHandle(clifd,stats);
        Close(clifd);
    }
}

enum SchedalgPolicy {
    SCHPOL_BLOCK,
    SCHPOL_HEAD,
    SCHPOL_RANDOM,
    SCHPOL_TAIL
};

void insertQueue(int connfd, struct timeval arrival_time) {
    //printf("DEBUG inserting to queue at %d.\n", queue_tail);
    queue[queue_tail].fd = connfd;
    queue[queue_tail].is_dropped = 0;
    queue[queue_tail].arrival_time = arrival_time;
    queue_tail = (queue_tail + 1) % queue_size;
    queue_count++;
}

void removeHead() {
    //printf("DEBUG removing head at %d.\n", queue_head);
    Close(queue[queue_head].fd);
    queue_head = (queue_head + 1) % queue_size;
    queue_count--;
}

int main(int argc, char *argv[])
{
    int listenfd, connfd, port, clientlen, threads;
    char *schedalg;
    struct sockaddr_in clientaddr;
    
    getargs(&port, &threads, &queue_size, &schedalg, argc, argv);

    // 
    // HW3: Create some threads...
    //

    enum SchedalgPolicy policy;
    if (strcmp(schedalg, "block") == 0) {
        policy = SCHPOL_BLOCK;
    } 
    else if (strcmp(schedalg, "dt") == 0) {
        policy = SCHPOL_TAIL;
    } 
    else if (strcmp(schedalg, "dh") == 0) {
        policy = SCHPOL_HEAD;
    }
    else if (strcmp(schedalg, "random") == 0) {
        policy = SCHPOL_RANDOM;
    }
    else {
        fprintf(stderr, "unknown schedalg.\n");
        exit(1);
    }

    queue_count = 0, queue_tail = 0, queue_head = 0, threads_working = 0;
    pthread_cond_init(&queue_empty_cond, NULL);
    pthread_cond_init(&queue_full_cond, NULL);
    if (0 != pthread_mutex_init(&queue_m, NULL)) {
        fprintf(stderr, "pthread_mutex_init failed.\n");
        exit(1);
    }

    queue = (queue_entry_t*)malloc(sizeof(queue_entry_t)*queue_size);

    pthread_t *pool = (pthread_t*)malloc(sizeof(pthread_t)*threads);
    for (int i = 0; i < threads; i++) {
        int *thread_id = (int*)malloc(sizeof(int));
        *thread_id = i;
        if (0 != pthread_create(&pool[i], NULL, workerThread, thread_id)) {
            fprintf(stderr, "pthread_create failed.\n");
            exit(1);
        }
    }

    listenfd = Open_listenfd(port);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);
        struct timeval arrival_time;
        gettimeofday(&arrival_time,NULL);
        if (DEBUG) printf("DEBUG master thread: accepted new client\n");

        pthread_mutex_lock(&queue_m);
        if ((queue_count + threads_working) == queue_size) { 
            if (DEBUG) printf("DEBUG master thread: queue full.\n");
            switch(policy) {
                case SCHPOL_BLOCK: 
                    if (DEBUG) printf("DEBUG master thread: waiting...\n");
                    while ((queue_count + threads_working) == queue_size) {
                        pthread_cond_wait(&queue_full_cond, &queue_m);
                    }
                    insertQueue(connfd,arrival_time);
                    if (queue_count == 1) {
                        if (DEBUG) printf("DEBUG master thread: queue not empty(i was full), signaling...\n");
                        pthread_cond_broadcast(&queue_empty_cond);
                    }
                    break; 
                case SCHPOL_TAIL:
                    if (DEBUG) printf("DEBUG master thread: dropping tail...\n");
                    Close(connfd);
                    break;
                case SCHPOL_HEAD:
                    if (DEBUG) printf("DEBUG master thread: dropping head...\n");
                    if (queue_count == 0) {
                        Close(connfd);
                        break;
                    }
                    removeHead();
                    insertQueue(connfd,arrival_time);
                    if (queue_count == 1) {
                        if (DEBUG) printf("DEBUG master thread: queue not empty(i was full), signaling...\n");
                        pthread_cond_broadcast(&queue_empty_cond);
                    }
                    break;
                case SCHPOL_RANDOM:
                    if (DEBUG) printf("DEBUG master thread: dropping random...\n");
                    if (queue_count == 0) {
                        Close(connfd);
                        break;
                    }
                    srand(time(NULL));
                    int num_to_drop = ((queue_count / 2) + (queue_count % 2));
                    for (int i = 0; i < num_to_drop; i++) {
                        int j;
                        do
                        {
                            int item_num = rand() % queue_count;
                            j = (queue_head + item_num) % queue_size;
                        } while(queue[j].is_dropped);
                        
                        queue[j].is_dropped = 1;
                        Close(queue[j].fd);
                    }
                    int i = queue_head;
                    int j = queue_head;
                    for (int counter = 0; counter < num_to_drop; counter++)
                    {
                        while (queue[j].is_dropped) {
                            j = (j + 1) % queue_size;
                        }
                        queue[i] = queue[j];
                        j = (j + 1) % queue_size;
                        i = (i + 1) % queue_size;
                    }
                    queue_count -= num_to_drop;
                    queue_tail = (queue_head + queue_count) % queue_size;
                    insertQueue(connfd,arrival_time);
                    if (queue_count == 1) {
                        if (DEBUG) printf("DEBUG master thread: queue not empty(i was full), signaling...\n");
                        pthread_cond_broadcast(&queue_empty_cond);
                    }
                    break;
                default:
                    if (DEBUG) printf("DEBUG master thread: no sched alg???");
                    exit(1);
                    break;
            }
        }
        else {
            insertQueue(connfd,arrival_time);
            if (queue_count == 1) {
                if (DEBUG) printf("DEBUG master thread: queue not empty, signaling...\n");
                pthread_cond_broadcast(&queue_empty_cond);
            }
        }
        
        pthread_mutex_unlock(&queue_m);
    }

}


    


 
