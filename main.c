/**
 * Redistribution of this file is permitted under the GNU General
 * Public License v2.
 *
 * Copyright 2012 by Gabriel Parmer.
 * Author: Gabriel Parmer, gparmer@gwu.edu, 2012
 */
/* 
 * This is a HTTP server.  It accepts connections on port 8080, and
 * serves a local static document.
 *
 * The clients you can use are 
 * - httperf (e.g., httperf --port=8080),
 * - wget (e.g. wget localhost:8080 /), 
 * - or even your browser.  
 *
 * To measure the efficiency and concurrency of your server, use
 * httperf and explore its options using the manual pages (man
 * httperf) to see the maximum number of connections per second you
 * can maintain over, for example, a 10 second period.
 *
 * Example usage:
 * # make test1
 * # make test2
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/wait.h>
#include <pthread.h>

#include <util.h> 		/* client_process */
#include <server.h>		/* server_accept and server_create */

#include <cas.h>

#define MAX_DATA_SZ 1024
#define MAX_CONCURRENCY 4

/* 
 * This is the function for handling a _single_ request.  Understand
 * what each of the steps in this function do, so that you can handle
 * _multiple_ requests.  Use this function as an _example_ of the
 * basic functionality.  As you increase the server in functionality,
 * you will want to probably keep all of the functions called in this
 * function, but define different code to use them.
 */
void
server_single_request(int accept_fd)
{
	int fd;

	/* 
	 * The server thread will always want to be doing the accept.
	 * That main thread will want to hand off the new fd to the
	 * new threads/processes/thread pool.
	 */
	fd = server_accept(accept_fd);
	client_process(fd);

    /* 
	 * A loop around these two lines will result in multiple
	 * documents being served.
	 */

	return;
}

/* 
 * The following implementations use a thread pool.  This collection
 * of threads is of maximum size MAX_CONCURRENCY, and is created by
 * pthread_create.  These threads retrieve data from a shared
 * data-structure with the main thread.  The synchronization around
 * this shared data-structure is either done using mutexes + condition
 * variables (for a bounded structure), or compare and swap (__cas in
 * cas.h) to do lock-free synchronization on a stack or ring buffer.
 */

pthread_t *thd_pool[MAX_CONCURRENCY+1]; // size of the thread pool and a master thread
pthread_mutex_t job_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
int job_queue[256]; // can queue 256 jobs in maximum

void add_job(int job_fd)
{
    int i;
    for(i = 0; i<255; i++)
    {
        if(i == 255 && &job_queue[i] != NULL)
        {
            printf("job queue is full!\n");
            break;
        }

        if(&job_queue[i] == NULL)
            job_queue[i] = job_fd;
    }

    return;
}

int
get_job()
{
    int i, job_fd;

    for(i = 0; i < 255; i++)
    {
        if(i == 255 && &job_queue[i] == NULL)
        {
            printf("no job to do\n");

            int *tmp;
            tmp = &job_fd;
            tmp = NULL;
            job_fd = *tmp;
            break;
        }

        if(&job_queue[i] != NULL)
        {
            job_fd = job_queue[i];
        }
    }
    return job_fd;
}

void
master_thread_routine(int accept_fd)
{
    int fd;
    while(1)
    {
        fd = server_accept(accept_fd);
        pthread_mutex_lock(&job_queue_mutex);
        add_job(fd);
        pthread_mutex_unlock(&job_queue_mutex);
    }
}

void 
worker_thread_routine()
{
    int fd;
    pthread_mutex_lock(&job_queue_mutex);
    fd = get_job();
    pthread_mutex_unlock(&job_queue_mutex);
    client_process(fd);
}

void
server_thread_pool_bounded(int accept_fd)
{
    pthread_t master_thread, worker_thread[MAX_CONCURRENCY];

    pthread_create(&master_thread, NULL, (void*)master_thread_routine, (void*)&accept_fd);

	int worker_id;
    for(worker_id = 0; worker_id < MAX_CONCURRENCY; worker_id++)
    {
        pthread_create(&worker_thread[worker_id], NULL, (void*)worker_thread_routine, (void*)&accept_fd);
    }

    return;
}

void
server_thread_pool_lock_free(/*int accept_fd*/)
{
	return;
}

typedef enum {
	SERVER_TYPE_ONE = 0,
	SERVER_TYPE_THREAD_POOL_BOUND = 1,
	SERVER_TYPE_THREAD_POOL_ATOMIC,
} server_type_t;

int
main(int argc, char *argv[])
{
	server_type_t server_type;
	short int port;
	int accept_fd;

	if (argc != 3) {
		printf("Proper usage of http server is:\n%s <port> <#>\n"
		       "port is the port to serve on, # is either\n"
		       "0: server only a single request\n"
		       "1: use a thread pool and a _bounded_ buffer with "
		       "mutexes + condition variables\n"
		       "2: use a thread pool and compare and swap to "
		       "implement a lock-free stack of requests\n",
		       argv[0]);
		return -1;
	}

	port = atoi(argv[1]);
	accept_fd = server_create(port);
	if (accept_fd < 0) return -1;
	
	server_type = atoi(argv[2]);

	switch(server_type) {
	case SERVER_TYPE_ONE:
		server_single_request(accept_fd);
		break;
	case SERVER_TYPE_THREAD_POOL_BOUND:
		server_thread_pool_bounded(accept_fd);
		break;
	case SERVER_TYPE_THREAD_POOL_ATOMIC:
		server_thread_pool_lock_free(accept_fd);
		break;
	}
	close(accept_fd);

	return 0;
}
