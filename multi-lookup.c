/******************************************************************************
 * FILE: multi-lookup.c
 * AUTHOR: Stephen Bennett
 * PROJECT: CSCI 3753 Programming Assignment 2
 * CREATE DATE: 02/22/2013
 * MODIFY DATE: 02/22/2013
 * DESCRIPTION:
 *  A multi-threaded application that resolves domain names to IP addresses.
 *  The application is composed of two sub-systems, each with one thread pool:
 *  requesters and resolvers. The sub-systems communicate with each other using
 *  a bounded queue.
 *
 ******************************************************************************/

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
/*#include <stdlib.h>*/
#include <unistd.h>     // Provides usleep

#include "multi-lookup.h"
#include "queue.h"
#include "util.h"


/* Requires: <exe_name> <input_file>+ <results_file> */
#define MIN_ARGS                3
#define USAGE                   "<inputFilePath> [inputFilePath...] <outputFilePath>"
#define MIN_RESOLVER_THREADS    2       // Mandatory lower-limit
#define MAX_NAME_LENGTH         256     // Maximum hostname length
#define INPUTFS                 "%255s"
#define QUEUE_SIZE              30


/* Prototypes for Local Functions */
void* requester(void* inputFilePath);
void* resolver();


/* Setup Shared/Global Variables */
FILE* outputfd = NULL;
queue buffer;
sem_t full, empty;
pthread_mutex_t qmutex, fmutex;
int reqFinished = 0;
int resFinished = 0;


int main(int argc, char *argv[])
{
    /* Setup Local Variables */
    unsigned int i;
    int rc;             // Return code from pthread_create() call
    void* status = 0;   // Return value from thread from pthread_join() call
    /* Create one requester thread per input file */
    unsigned int numRequesterThreads = argc - 2;
    /* TODO: set numResolverThreads equal to the number of cores available */
    unsigned int numResolverThreads = 2;

    /* Verify Correct Usage */
    if (argc < MIN_ARGS) {
        fprintf(stderr, "Usage Error: Not enough arguments: %d\n", (argc - 1));
        fprintf(stderr, "Usage:\n  %s %s\n", argv[0], USAGE);
        return ERR_ARGS;
    }

    /* Verify Minimum Resolver Thread Limit */
    if (numResolverThreads < MIN_RESOLVER_THREADS) {
        fprintf(stderr, "Warning: Program must provide at least %d resolver threads",
                MIN_RESOLVER_THREADS);
        numResolverThreads = 2;
    }

    /* Setup Requester/Resolver Thread Arrays */
    pthread_t reqThreads[numRequesterThreads];
    pthread_t resThreads[numResolverThreads];

    /* Open Output File */
    outputfd = fopen(argv[argc-1], "w");
    if (!outputfd) {
        fprintf(stderr, "File Error: Error opening output file [%s]: %s\n",
                argv[argc-1], strerror(errno));
        return ERR_FOPEN;
    }

    /* Initialize Bounded Queue */
    if (queue_init(&buffer, QUEUE_SIZE) == QUEUE_FAILURE) {
        fprintf(stderr, "Queue Error: init failed!\n");
    }

    /* Initialize Semaphores */
    sem_init(&full, 0, QUEUE_SIZE);
    sem_init(&empty, 0, 0);
    pthread_mutex_init(&qmutex, NULL);
    pthread_mutex_init(&fmutex, NULL);

    /* Spawn Requester Threads */
    for (i = 0; i < numRequesterThreads; ++i) {
        rc = pthread_create(&reqThreads[i], NULL, requester, argv[i+1]);
        if (rc) {
            fprintf(stderr, "Pthread Error: Return code from pthread_create() is %d\n", rc);
            return ERR_PTHREAD_CREATE;
        }
    }

    /* Spawn Resolver Threads */
    for (i = 0; i < numResolverThreads; ++i) {
        rc = pthread_create(&resThreads[i], NULL, resolver, NULL);
        if (rc) {
            fprintf(stderr, "Pthread Error: Return code from pthread_create() is %d\n", rc);
            return ERR_PTHREAD_CREATE;
        }
    }

    /* Wait for All Requester Threads to Finish */
    for (i = 0; i < numRequesterThreads; ++i) {
        pthread_join(reqThreads[i], &status);
        //if (!status) {
            //print
        //}
    }

    printf("FINISHED REQUESTER THREADS\n");

    /* Notify resolver threads that all hostnames have be read in */
    reqFinished = 1;

    /* Wait for All Resolver Threads to Finish */
    for (i = 0; i < numResolverThreads; ++i) {
        pthread_join(resThreads[i], &status);
    }

    /* Close Output File */
    fclose(outputfd);

    /* Cleanup Queue Memory */
    queue_cleanup(&buffer);

    /* Cleanup Semaphore Memory */
    sem_destroy(&full);
    sem_destroy(&empty);
    pthread_mutex_destroy(&qmutex);
    pthread_mutex_destroy(&fmutex);

    return EXIT_SUCCESS;
}


void* requester(void* inputFilePath)
{
    FILE* inputfd = NULL;
    char hostname[MAX_NAME_LENGTH];
    char* payload;


    /* Open Input File */
    inputfd = fopen((char*) inputFilePath, "r");
    if (!inputfd) {
        fprintf(stderr, "File Error: Error opening input file [%s]: %s\n",
                (char*) inputFilePath, strerror(errno));
        return (void*) ERR_FOPEN;
    }

    /* Read File and Process */
    while (fscanf(inputfd, INPUTFS, hostname) > 0) {
        /* Must make a copy of the hostname to be placed in the queue;
         * otherwise, the queue will be full of pointers to hostname */
        if ((payload = (char*) malloc(sizeof(hostname))) == NULL) {
            fprintf(stderr, "Malloc Error: Error allocating memory for payload [%s]: %s\n",
                    hostname, strerror(errno));
            return (void*) ERR_MALLOC;
        }
        if (strncpy(payload, hostname, MAX_NAME_LENGTH) != payload) {
            fprintf(stderr, "Strncpy Error: Error copying string [%s]\n",
                    hostname);
            return (void*) ERR_STRNCPY;
        }

        /* Make sure the queue is not full */
        sem_wait(&full);

        /* Acquire exclusive access to queue so it can be updated safely */
        pthread_mutex_lock(&qmutex);

        /* Add hostname to Bounded Queue */
        if (queue_push(&buffer, (void*) payload) == QUEUE_FAILURE) {
            fprintf(stderr, "Queue Error: push [%s] failed!\n", payload);
        }
        printf("Pushed: %s\n", payload);

        /* Release exclusive access to queue */
        pthread_mutex_unlock(&qmutex);

        /* Notify resolver threads about new hostname in queue */
        sem_post(&empty);
    }

    /* Close Input File */
    fclose(inputfd);

    return NULL;
}


void* resolver()
{
    char* hostname;
    char resolvedIP[INET6_ADDRSTRLEN];

    pthread_mutex_lock(&qmutex);
    while (!(reqFinished && queue_is_empty(&buffer))) {
        pthread_mutex_unlock(&qmutex);
        /* Wait for queue to not be empty */
        sem_wait(&empty);

        /* Acquire exclusive access to queue so it can be read safely */
        pthread_mutex_lock(&qmutex);

        /* Read hostname from Bounded Queue */
        if ((hostname = (char*) queue_pop(&buffer)) == NULL) {
            fprintf(stderr, "Queue Error: pop failed!\n");
        }
        if (reqFinished && queue_is_empty(&buffer)) {
            printf("RESOLVER THREADS FINISHED\n");
            resFinished = 1;
        }

        /* Release exclusive access to queue */
        pthread_mutex_unlock(&qmutex);

        /* Notify requester threads that there is more room in queue */
        sem_post(&full);

        printf("Popped: %s\n", hostname);

        /* Lookup hostname and get IP string */
        if (dnslookup(hostname, resolvedIP, sizeof(resolvedIP))
                == UTIL_FAILURE) {
            fprintf(stderr, "DNSlookup Error: %s\n", hostname);
            strncpy(resolvedIP, "", sizeof(resolvedIP));
        }

        /* Acquire exclusive access to output file */
        pthread_mutex_lock(&fmutex);

        /* Write to Output File */
        fprintf(outputfd, "%s,%s\n", hostname, resolvedIP);

        /* Release exclusive access to output file */
        pthread_mutex_unlock(&fmutex);

        /* Free malloc'd Memory */
        free(hostname);

        pthread_mutex_lock(&qmutex);
    }
    pthread_mutex_unlock(&qmutex);

    return NULL;
}

