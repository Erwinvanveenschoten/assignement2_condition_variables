/*
 * Operating Systems {2INCO} Practical Assignment
 * Condition Variables Application
 *
 * Arjon Arts (1521950)
 * Erwin van Veenschoten (1524348)
 *
 * Grading:
 * Students who hand in clean code that fully satisfies the minimum requirements will get an 8.
 * Extra steps can lead to higher marks because we want students to take the initiative.
 * Extra steps can be, for example, in the form of measurements added to your code, a formal
 * analysis of deadlock freeness etc.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include "prodcons.h"

#define BUFFER_EMPTY 0

/*******************************************************************************
**  FUNCTION DECLARATIONS
*******************************************************************************/
static void rsleep (int t);			  // already implemented (see below)
static ITEM get_next_item (void);	// already implemented (see below)

static void put(ITEM item);
static ITEM get( void );
static void * consumer(void * arg);
static void * producer(void *arg);

/*******************************************************************************
**  GLOBAL DATA
*******************************************************************************/

static pthread_mutex_t critical_section = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t buffer_full, buffer_empty;
static int item_count, production_index, consumption_index, next_item_to_produce = 0;
static ITEM buffer[BUFFER_SIZE];

/*******************************************************************************
**  MAIN
*******************************************************************************/

int main (void)
{
    pthread_t producers_tid[NROF_PRODUCERS], consumer_tid;

    // create the producer threads and the consumer thread
    pthread_create(&consumer_tid, NULL, consumer, NULL);

    for (int i = 0; i < NROF_PRODUCERS; i++)
    {
      pthread_create(&producers_tid[i], NULL, producer, NULL);
    }

    // wait until all threads are finished
    pthread_join(consumer_tid, NULL);

    for (int i = 0; i < NROF_PRODUCERS; i++)
    {
      pthread_join(producers_tid[i], NULL);
    }

    // destroy mutex
    pthread_mutex_destroy(&critical_section);

    // destroy conditional variables
    pthread_cond_destroy(&buffer_full);
    pthread_cond_destroy(&buffer_empty);

    return (0);
}

/*******************************************************************************
**  FUNCTION DEFINITIONS
*******************************************************************************/

/* producer thread */
static void *
producer (void * arg)
{

  ITEM item;

    while (true)
    {
        item = get_next_item();

        rsleep (100);	// simulating all kind of activities...

        // mutex-lock;
        pthread_mutex_lock(&critical_section);

        // while buffer is full or item is not next item to process
        while ( item_count == BUFFER_SIZE || (item_count != BUFFER_SIZE && item != next_item_to_produce))
        {
          // wait-cv;
          pthread_cond_wait(&buffer_empty, &critical_section);

          // if item is not next item to process
          if ( item != next_item_to_produce )
          {
            // signal other producer
            pthread_cond_signal(&buffer_empty);
          }
        }
        // critical-section;

        // increase nex item to process
        next_item_to_produce++;

        // check if next item to process has exceeded max items
        if(next_item_to_produce > NROF_ITEMS)
        {
          next_item_to_produce = NROF_ITEMS;
        }

        // put item in buffer
        put(item);

        //  Signal to consumer since buffer is no longer empty
        pthread_cond_signal(&buffer_full);

        // mutex-unlock;
        pthread_mutex_unlock(&critical_section);

        // when last item is produced
        if (item == NROF_ITEMS)
        {
          break;
        }
    }
    return (NULL);
}

/* consumer thread */
static void *
consumer (void * arg)
{
    int prod_terminate_count = 0;
    while ( true )
    {
        // mutex-lock;
        pthread_mutex_lock(&critical_section);

        // while buffer is empty
        while ( item_count == BUFFER_EMPTY )
        {
          //wait
          pthread_cond_wait(&buffer_full,&critical_section);
        }

        // critical-section;
        ITEM item = get();

        // signal producer since buffer is no longer full
        pthread_cond_signal(&buffer_empty);

        // mutex-unlock;
        pthread_mutex_unlock(&critical_section);

        //  Increment finished producers when item with value NROF_ITEMS is obtained from the buffer
        if (item == NROF_ITEMS)
        {
          prod_terminate_count++;
        } else
        {
          printf("%d\n", item);
        }

        //  If all producers are finished, terminate...
        if (prod_terminate_count == NROF_PRODUCERS)
        {
          break;
        }

        rsleep (100);		// simulating all kind of activities...
    }

	  return (NULL);
}

/*
 * rsleep(int t)
 *
 * The calling thread will be suspended for a random amount of time between 0 and t microseconds
 * At the first call, the random generator is seeded with the current time
 */
static void
rsleep (int t)
{
    static bool first_call = true;

    if (first_call == true)
    {
        srandom (time(NULL));
        first_call = false;
    }
    usleep (random () % t);
}


/*
 * get_next_item()
 *
 * description:
 *		thread-safe function to get a next job to be executed
 *		subsequent calls of get_next_item() yields the values 0..NROF_ITEMS-1
 *		in arbitrary order
 *		return value NROF_ITEMS indicates that all jobs have already been given
 *
 * parameters:
 *		none
 *
 * return value:
 *		0..NROF_ITEMS-1: job number to be executed
 *		NROF_ITEMS:		 ready
 */
static ITEM
get_next_item(void)
{
  static pthread_mutex_t	job_mutex	= PTHREAD_MUTEX_INITIALIZER;
	static bool 			jobs[NROF_ITEMS+1] = { false };	// keep track of issued jobs
	static int              counter = 0;    // seq.nr. of job to be handled
    ITEM 					found;          // item to be returned

	/* avoid deadlock: when all producers are busy but none has the next expected item for the consumer
	 * so requirement for get_next_item: when giving the (i+n)'th item, make sure that item (i) is going to be handled (with n=nrof-producers)
	 */
	pthread_mutex_lock (&job_mutex);

    counter++;
	if (counter > NROF_ITEMS)
	{
	    // we're ready
	    found = NROF_ITEMS;
	}
	else
	{
	    if (counter < NROF_PRODUCERS)
	    {
	        // for the first n-1 items: any job can be given
	        // e.g. "random() % NROF_ITEMS", but here we bias the lower items
	        found = (random() % (2*NROF_PRODUCERS)) % NROF_ITEMS;
	    }
	    else
	    {
	        // deadlock-avoidance: item 'counter - NROF_PRODUCERS' must be given now
	        found = counter - NROF_PRODUCERS;
	        if (jobs[found] == true)
	        {
	            // already handled, find a random one, with a bias for lower items
	            found = (counter + (random() % NROF_PRODUCERS)) % NROF_ITEMS;
	        }
	    }

	    // check if 'found' is really an unhandled item;
	    // if not: find another one
	    if (jobs[found] == true)
	    {
	        // already handled, do linear search for the oldest
	        found = 0;
	        while (jobs[found] == true)
            {
                found++;
            }
	    }
	}
    jobs[found] = true;


	pthread_mutex_unlock (&job_mutex);
	return (found);
}

void put(ITEM item)
{
  buffer[production_index] = item;
  production_index = (++production_index) % BUFFER_SIZE;

  item_count++;
}

ITEM get( void )
{
  ITEM item = buffer[consumption_index];
  consumption_index = (++consumption_index) % BUFFER_SIZE;
  item_count--;
  return item;
}
