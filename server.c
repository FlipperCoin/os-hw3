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

int *queue;
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
        
        if (!first_run) threads_working--;
        first_run = 0;
        
        if (queue_count < 1) printf("DEBUG worker thread: queue empty, waiting...\n");
        while (queue_count < 1) {
            pthread_cond_wait(&queue_empty_cond, &queue_m);
        }

        int clifd = queue[queue_head];
        queue_head = (queue_head + 1) % queue_size;
        queue_count--;
        threads_working++;

        // if not full anymore
        if (queue_count + threads_working == queue_size-1) {
            printf("DEBUG worker thread: queue not full, signaling...\n");
            pthread_cond_signal(&queue_full_cond);
        }

        printf("DEBUG worker thread: handling request.\n");
        pthread_mutex_unlock(&queue_m);

        requestHandle(clifd);
        Close(clifd);
    }
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

    queue_count = 0, queue_tail = 0, queue_head = 0, threads_working = 0;
    pthread_cond_init(&queue_empty_cond, NULL);
    pthread_cond_init(&queue_full_cond, NULL);
    if (0 != pthread_mutex_init(&queue_m, NULL)) {
        fprintf(stderr, "pthread_mutex_init failed.\n");
    }

    queue = (int*)malloc(sizeof(int)*queue_size);

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
        while ((queue_count + threads_working) == queue_size) {
            printf("DEBUG master thread: queue full, waiting...\n");
            pthread_cond_wait(&queue_full_cond, &queue_m);
        }
        
        queue[queue_tail] = connfd;
        queue_tail = (queue_tail + 1) % queue_size;
        queue_count++;
        if (queue_count == 1) {
            printf("DEBUG master thread: queue not empty, signaling...\n");
            pthread_cond_signal(&queue_empty_cond);
        }

        pthread_mutex_unlock(&queue_m);
    }

}


    


 
