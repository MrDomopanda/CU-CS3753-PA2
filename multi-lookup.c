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
#define QUEUE_SIZE              10


/* Prototypes for Local Functions */
void* requester(void* inputFilePath);
void* resolver();


/* Setup Shared/Global Variables */
FILE* outputfd = NULL;
queue buffer;


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

    /* Initialize Bounded Queue */
    if (queue_init(&buffer, QUEUE_SIZE) == QUEUE_FAILURE) {
        fprintf(stderr, "Queue Error: init failed!\n");
    }

    /* Open Output File */
    outputfd = fopen(argv[argc-1], "w");
    if (!outputfd) {
        fprintf(stderr, "File Error: Error opening output file [%s]: %s\n",
                argv[argc-1], strerror(errno));
        return ERR_FOPEN;
    }

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

    /* Wait for All Resolver Threads to Finish */
    for (i = 0; i < numResolverThreads; ++i) {
        pthread_join(resThreads[i], &status);
    }

    /* Close Output File */
    fclose(outputfd);

    /* Cleanup Queue Memory */
    queue_cleanup(&buffer);

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

        /* Add hostname to Bounded Queue */
        printf("Pushed: %s\n", payload);
        if (queue_push(&buffer, (void*) payload) == QUEUE_FAILURE) {
            fprintf(stderr, "Queue Error: push [%s] failed!\n", payload);
        }
    }

    while (!queue_is_empty(&buffer)) {
        if ((payload = (char*) queue_pop(&buffer)) == NULL) {
            fprintf(stderr, "Queue Error: pop failed!\n");
        }
        printf("Popped: %s\n", payload);
        free(payload);
    }

    /* Close Input File */
    fclose(inputfd);

    return NULL;
}


void* resolver()
{
    return NULL;
    char* hostname;
    char firstipstr[INET6_ADDRSTRLEN];

    /* Read hostname from Bounded Queue */
    while (!queue_is_empty(&buffer)) {
        if ((hostname = (char*) queue_pop(&buffer)) == NULL) {
            fprintf(stderr, "Queue Error: pop failed!\n");
        }
        printf("Popped: %s\n", hostname);
        free(hostname);
    }

    return NULL;
}

