/******************************************************************************
 * FILE: multi-lookup.c
 * AUTHOR: Stephen Bennett
 * PROJECT: CSCI 3753 Programming Assignment 2
 * CREATE DATE: 02/22/2013
 * MODIFY DATE: 02/22/2013
 * DESCRIPTION:
 *  A multi-threaded application that resolves domain names to IP addresses.
 *  The application is composed of two sub-systems, each with one thread pool:
 *  requesters and resolvers.
 *  The number of requester threads spawned is based on the number of input
 *  files given.
 *  The number of resolver threads spawned is based dynamically on the number
 *  of cores available on the machine running the executable.
 *  The two sub-systems communicate with each other using
 *  a bounded queue.
 *
 ******************************************************************************/

#include "multi-lookup.h"

/* Uncomment the following line to enable debugging output */
//#define LOOKUP_DEBUG

/* Setup Shared/Global Variables */
FILE*           outputfd = NULL;
int             reqRunning; // Flag to notify resolvers when all requesters done
queue           buffer;     // Shared buffer
sem_t           full,       // Synch, requesters wait if queue full
                empty,      // Synch, resolvers wait if queue empty
                resBegin;   // Synch, requesters notify resolvers to begin
pthread_mutex_t qmutex,     // Mutex for queue
                fmutex;     // Mutex for output file


int main(int argc, char *argv[])
{
    /* Setup Local Variables */
    unsigned int i;
    int rc;             // Return code from pthread_create() call
    void* status = 0;   // Return value from thread from pthread_join() call
    /* Create one requester thread per input file */
    unsigned int numRequesterThreads = argc - 2;
    reqRunning = numRequesterThreads;
    /* Create as many resolver threads as cores */
    unsigned int numResolverThreads = sysconf( _SC_NPROCESSORS_ONLN );

    /* Verify Correct Usage */
    if (argc < MIN_ARGS) {
        fprintf(stderr, "USAGE ERROR: Not enough arguments: %d\n", (argc - 1));
        fprintf(stderr, "Usage:\n  %s %s\n", argv[0], USAGE);
        return ERR_ARGS;
    }

    /* Verify Minimum Resolver Thread Limit */
    if (numResolverThreads < MIN_RESOLVER_THREADS) {
        fprintf(stderr, "WARNING: Program must provide at least %d resolver threads",
                MIN_RESOLVER_THREADS);
        numResolverThreads = 2;
    }

    /* Setup Requester/Resolver Thread Arrays */
    pthread_t reqThreads[numRequesterThreads];
    pthread_t resThreads[numResolverThreads];

    /* Open Output File */
    outputfd = fopen(argv[argc-1], "w");
    if (!outputfd) {
        fprintf(stderr, "FILE ERROR: Error opening output file [%s]: %s\n",
                argv[argc-1], strerror(errno));
        return ERR_FOPEN;
    }

    /* Initialize Bounded Queue */
    if (queue_init(&buffer, QUEUE_SIZE) == QUEUE_FAILURE) {
        fprintf(stderr, "QUEUE ERROR: init failed!\n");
    }

    /* Initialize Semaphores and Mutexes */
    if (sem_init(&full, 0, QUEUE_SIZE)) {
        fprintf(stderr, "SEMAPHORE ERROR: Error initializing semaphore 'full': %s\n",
                strerror(errno));
        return ERR_SEMAPHORE;
    }
    if (sem_init(&empty, 0, 0)) {
        fprintf(stderr, "SEMAPHORE ERROR: Error initializing semaphore 'empty': %s\n",
                strerror(errno));
        return ERR_SEMAPHORE;
    }
    if (sem_init(&resBegin, 0, 0)) {
        fprintf(stderr, "SEMAPHORE ERROR: Error initializing semaphore 'resBegin': %s\n",
                strerror(errno));
        return ERR_SEMAPHORE;
    }
    if (pthread_mutex_init(&qmutex, NULL)) {
        fprintf(stderr, "MUTEX ERROR: Error initializing mutex 'qmutex': %s\n",
                strerror(errno));
        return ERR_MUTEX;
    }
    if (pthread_mutex_init(&fmutex, NULL)) {
        fprintf(stderr, "MUTEX ERROR: Error initializing mutex 'fmutex': %s\n",
                strerror(errno));
        return ERR_MUTEX;
    }

    /* Spawn Requester Threads */
    for (i = 0; i < numRequesterThreads; ++i) {
        rc = pthread_create(&reqThreads[i], NULL, requester, argv[i+1]);
        if (rc) {
            fprintf(stderr, "PTHREAD ERROR: Return code from pthread_create() is %d\n", rc);
            return ERR_PTHREAD_CREATE;
        }
    }

    /* Spawn Resolver Threads */
    for (i = 0; i < numResolverThreads; ++i) {
        rc = pthread_create(&resThreads[i], NULL, resolver, NULL);
        if (rc) {
            fprintf(stderr, "PTHREAD ERROR: Return code from pthread_create() is %d\n", rc);
            return ERR_PTHREAD_CREATE;
        }
    }

    /* Wait for All Requester Threads to Finish */
    for (i = 0; i < numRequesterThreads; ++i) {
        pthread_join(reqThreads[i], &status);
#ifdef LOOKUP_DEBUG
        printf("REQUESTER THREAD #%d FINISHED\n", i+1);
#endif
    }

    /* In case all input files were bogus, notify resolver threads to stop sleeping */
    sem_post(&resBegin);

#ifdef LOOKUP_DEBUG
    printf("FINISHED ALL REQUESTER THREADS\n");
#endif

    /* Wait for All Resolver Threads to Finish */
    for (i = 0; i < numResolverThreads; ++i) {
        pthread_join(resThreads[i], &status);
#ifdef LOOKUP_DEBUG
        printf("RESOLVER THREAD #%d FINISHED\n", i+1);
#endif
    }

#ifdef LOOKUP_DEBUG
    printf("FINISHED ALL RESOLVER THREADS\n");
#endif

    /* Close Output File */
    if (fclose(outputfd)) {
        fprintf(stderr, "FILE ERROR: Error closing output file [%s]: %s\n",
                argv[argc-1], strerror(errno));
    }

    /* Cleanup Queue Memory */
    queue_cleanup(&buffer);

    /* Cleanup Semaphore Memory */
    if (sem_destroy(&full)) {
        fprintf(stderr, "SEMAPHORE ERROR: Error destroying semaphore 'full': %s\n",
                strerror(errno));
    }
    if (sem_destroy(&empty)) {
        fprintf(stderr, "SEMAPHORE ERROR: Error destroying semaphore 'empty': %s\n",
                strerror(errno));
    }
    if (sem_destroy(&resBegin)) {
        fprintf(stderr, "SEMAPHORE ERROR: Error destroying semaphore 'resBegin': %s\n",
                strerror(errno));
    }
    if (pthread_mutex_destroy(&qmutex)) {
        fprintf(stderr, "MUTEX ERROR: Error destroying mutex 'qmutex': %s\n",
                strerror(errno));
    }
    if (pthread_mutex_destroy(&fmutex)) {
        fprintf(stderr, "MUTEX ERROR: Error destroying mutex 'fmutex': %s\n",
                strerror(errno));
    }

    return EXIT_SUCCESS;
}


void* requester(void* inputFilePath)
{
    FILE* inputfd = NULL;
    char* payload;
    char hostname[MAX_NAME_LENGTH];

    /* Open Input File */
    inputfd = fopen((char*) inputFilePath, "r");
    if (!inputfd) {
        fprintf(stderr, "FILE ERROR: Error opening input file [%s]: %s\n",
                (char*) inputFilePath, strerror(errno));

        /* Notify resolver threads that a requester thread has finished */
        pthread_mutex_lock(&qmutex);
        reqRunning--;
        pthread_mutex_unlock(&qmutex);

        return (void*) ERR_FOPEN;
    }

    /* Notify resolver threads that at least one input file has been opened (is not bogus) */
    sem_post(&resBegin);

    /* Read File and Process */
    while (fscanf(inputfd, INPUTFS, hostname) > 0) {
        /* Must make a copy of the hostname to be placed in the queue;
         * otherwise, the queue will be full of pointers to hostname */
        if ((payload = (char*) malloc(sizeof(hostname))) == NULL) {
            fprintf(stderr, "MALLOC ERROR: Error allocating memory for payload [%s]: %s\n",
                    hostname, strerror(errno));

            /* Notify resolver threads that a requester thread has finished */
            pthread_mutex_lock(&qmutex);
            reqRunning--;
            pthread_mutex_unlock(&qmutex);

            return (void*) ERR_MALLOC;
        }
        if (strncpy(payload, hostname, MAX_NAME_LENGTH) != payload) {
            fprintf(stderr, "STRNCPY ERROR: Error copying string [%s]\n",
                    hostname);

            /* Notify resolver threads that a requester thread has finished */
            pthread_mutex_lock(&qmutex);
            reqRunning--;
            pthread_mutex_unlock(&qmutex);

            return (void*) ERR_STRNCPY;
        }

        /* Sleep for 0 to 100 microseconds - as per Section 2.2 of handout */
        usleep(rand() % 100);

        /* Make sure the queue is not full */
        sem_wait(&full);

        /* Acquire exclusive access to queue so it can be updated safely */
        pthread_mutex_lock(&qmutex);

        /* Add hostname to Bounded Queue */
        if (queue_push(&buffer, (void*) payload) == QUEUE_FAILURE) {
            fprintf(stderr, "QUEUE ERROR: push [%s] failed!\n", payload);
        }

        /* Release exclusive access to queue */
        pthread_mutex_unlock(&qmutex);

        /* Notify resolver threads about new hostname in queue */
        sem_post(&empty);

#ifdef LOOKUP_DEBUG
        printf("Pushed: %s\n", payload);
#endif
    }

    /* Close Input File */
    if (fclose(inputfd)) {
        fprintf(stderr, "FILE ERROR: Error closing input file [%s]: %s\n",
                (char*) inputFilePath, strerror(errno));
    }

    /* Notify resolver threads that a requester thread has finished */
    pthread_mutex_lock(&qmutex);
    reqRunning--;
    pthread_mutex_unlock(&qmutex);

    return NULL;
}


void* resolver()
{
    char* hostname;
    char resolvedIP[INET6_ADDRSTRLEN];

    /* Wait for at least one input file to be opened (non bogus)
     * and notify other sleeping resolver threads of the same
     */
    sem_wait(&resBegin);
    sem_post(&resBegin);

    /* Lock while checking reqRunning and queue */
    pthread_mutex_lock(&qmutex);
    while (reqRunning || !queue_is_empty(&buffer)) {
        pthread_mutex_unlock(&qmutex);

        /* Wait for queue to not be empty */
        sem_wait(&empty);

        /* Acquire exclusive access to queue so it can be read safely */
        pthread_mutex_lock(&qmutex);

        /* Read hostname from Bounded Queue */
        if ((hostname = (char*) queue_pop(&buffer)) == NULL) {
            fprintf(stderr, "QUEUE ERROR: pop failed!\n");
        }

        /* Release exclusive access to queue */
        pthread_mutex_unlock(&qmutex);

        /* Notify requester threads that there is more room in queue */
        sem_post(&full);

#ifdef LOOKUP_DEBUG
        printf("Popped: %s\n", hostname);
#endif

        /* Lookup hostname and get IP string */
        if (dnslookup(hostname, resolvedIP, sizeof(resolvedIP))
                == UTIL_FAILURE) {
            fprintf(stderr, "DNSLOOKUP ERROR: %s\n", hostname);
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

        /* Acquire lock to check WHILE condition */
        pthread_mutex_lock(&qmutex);
    }
    pthread_mutex_unlock(&qmutex);

    return NULL;
}
