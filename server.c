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

typedef struct {
    int fd;
    int is_dropped;
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
    int first_run = 1;
    while (1) {
        pthread_mutex_lock(&queue_m);   
        
        if (!first_run) {
            threads_working--;
            // if not full anymore
            if (queue_count + threads_working == queue_size-1) {
                printf("DEBUG worker thread: queue not full, signaling...\n");
                pthread_cond_signal(&queue_full_cond);
            }
        }
        first_run = 0;
        
        if (queue_count < 1) printf("DEBUG worker thread: queue empty, waiting...\n");
        while (queue_count < 1) {
            pthread_cond_wait(&queue_empty_cond, &queue_m);
        }

        printf("DEBUG worker thread: handling request in queue: %d.\n", queue_head);
        int clifd = queue[queue_head].fd;
        queue_head = (queue_head + 1) % queue_size;
        queue_count--;
        threads_working++;

        pthread_mutex_unlock(&queue_m);

        requestHandle(clifd);
        Close(clifd);
    }
}

enum SchedalgPolicy {
    SCHPOL_BLOCK,
    SCHPOL_HEAD,
    SCHPOL_RANDOM,
    SCHPOL_TAIL
};

void insertQueue(int connfd) {
    //printf("DEBUG inserting to queue at %d.\n", queue_tail);
    queue[queue_tail].fd = connfd;
    queue[queue_tail].is_dropped = 0;
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
        if (0 != pthread_create(&pool[i], NULL, workerThread, NULL)) {
            fprintf(stderr, "pthread_create failed.\n");
            exit(1);
        }
    }

    listenfd = Open_listenfd(port);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);
        printf("DEBUG master thread: accepted new client\n");

        pthread_mutex_lock(&queue_m);
        if ((queue_count + threads_working) == queue_size) { 
            printf("DEBUG master thread: queue full.\n");
            switch(policy) {
                case SCHPOL_BLOCK: 
                    printf("DEBUG master thread: waiting...\n");
                    while ((queue_count + threads_working) == queue_size) {
                        pthread_cond_wait(&queue_full_cond, &queue_m);
                    }
                    insertQueue(connfd);
                    if (queue_count == 1) {
                        printf("DEBUG master thread: queue not empty(i was full), signaling...\n");
                        pthread_cond_signal(&queue_empty_cond);
                    }
                    break; 
                case SCHPOL_TAIL:
                    printf("DEBUG master thread: dropping tail...\n");
                    Close(connfd);
                    break;
                case SCHPOL_HEAD:
                    printf("DEBUG master thread: dropping head...\n");
                    removeHead();
                    insertQueue(connfd);
                    if (queue_count == 1) {
                        printf("DEBUG master thread: queue not empty(i was full), signaling...\n");
                        pthread_cond_signal(&queue_empty_cond);
                    }
                    break;
                case SCHPOL_RANDOM:
                    printf("DEBUG master thread: dropping random...\n");
                    srand(time(NULL));
                    for (int i = 0; i < queue_count / 2; i++) {
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
                    for (int counter = 0; counter < ((queue_count / 2) + (queue_count % 2)); counter++)
                    {
                        while (queue[j].is_dropped) {
                            j = (j + 1) % queue_size;
                        }
                        queue[i] = queue[j];
                        j = (j + 1) % queue_size;
                        i = (i + 1) % queue_size;
                    }
                    queue_count -= ((queue_count / 2) + (queue_count % 2));
                    queue_tail = (queue_head + queue_count) % queue_size;
                    insertQueue(connfd);
                    if (queue_count == 1) {
                        printf("DEBUG master thread: queue not empty(i was full), signaling...\n");
                        pthread_cond_signal(&queue_empty_cond);
                    }
                    break;
                default:
                    printf("DEBUG master thread: no sched alg???");
                    exit(1);
                    break;
            }
        }
        else {
            insertQueue(connfd);
            if (queue_count == 1) {
                printf("DEBUG master thread: queue not empty, signaling...\n");
                pthread_cond_broadcast(&queue_empty_cond);
            }
        }
        
        pthread_mutex_unlock(&queue_m);
    }

}


    


 
