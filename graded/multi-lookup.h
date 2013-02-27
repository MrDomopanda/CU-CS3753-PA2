/******************************************************************************
 * FILE: multi-lookup.h
 * AUTHOR: Stephen Bennett
 * PROJECT: CSCI 3753 Programming Assignment 2
 * CREATE DATE: 02/22/2013
 * MODIFY DATE: 02/22/2013
 * DESCRIPTION:
 *  This file contains declarations for functions part of
 *      the multi-lookup program.
 *
 ******************************************************************************/

#ifndef MULTI_LOOKUP_H
#define MULTI_LOOKUP_H

/* Standard Includes */
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
//#include <stdlib.h>
#include <unistd.h>     // Provides usleep, num cores


/* Local Includes */
#include "queue.h"
#include "util.h"


/* Error code defines */
#define ERR_ARGS            -1
#define ERR_FOPEN           1
#define ERR_PTHREAD_CREATE  2
#define ERR_MALLOC          3
#define ERR_STRNCPY         4
#define ERR_SEMAPHORE       5
#define ERR_MUTEX           6


/* Miscellaneous Helpful Defines */
// Requires: <exe_name> <input_file>+ <results_file>
#define MIN_ARGS                3
#define USAGE                   "<inputFilePath> [inputFilePath...] <outputFilePath>"
#define MIN_RESOLVER_THREADS    2       // Mandatory lower-limit
#define MAX_NAME_LENGTH         256     // Maximum hostname length
#define INPUTFS                 "%255s"
#define QUEUE_SIZE              10


/* Prototypes for Local Functions */
void* requester(void* inputFilePath);
void* resolver();

#endif
