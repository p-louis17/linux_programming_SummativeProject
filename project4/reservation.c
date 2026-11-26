#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

#define QUEUE_CAPACITY      5   /* max pending reservations in the queue */
#define TOTAL_RESERVATIONS 15   /* fixed number of requests to process   */
#define MONITOR_INTERVAL    4   /* seconds between monitor reports       */

/* One reservation request. */
typedef struct {
    int reservation_id;   /* unique, incremental */
    int book_id;          /* even = available, odd = unavailable */
} Reservation;

/*
 * All shared data lives in ONE struct, protected by ONE mutex.
 * Rule: no thread reads or writes any field below unless it holds 'lock'.
 */
typedef struct {
    /* Circular buffer */
    Reservation buffer[QUEUE_CAPACITY];
    int head;    /* index where the consumer removes the next item */
    int tail;    /* index where the producer inserts the next item */
    int count;   /* number of items currently in the queue */

    /* Statistics shown by the monitor */
    int submitted;
    int processed;
    int approved;
    int rejected;

    /* Set to 1 once all reservations are processed, so the monitor can stop */
    int finished;

    /* Synchronization */
    pthread_mutex_t lock;
    pthread_cond_t  not_full;       /* producer waits on this when count == 5 */
    pthread_cond_t  not_empty;      /* consumer waits on this when count == 0 */
    pthread_cond_t  finished_cond;  /* monitor sleeps on this between reports */
} Library;

static Library lib;

/* Initialize counters, mutex and condition variables before any thread starts. */
void library_init(void)
{
    lib.head = 0;
    lib.tail = 0;
    lib.count = 0;
    lib.submitted = 0;
    lib.processed = 0;
    lib.approved = 0;
    lib.rejected = 0;
    lib.finished = 0;

    pthread_mutex_init(&lib.lock, NULL);
    pthread_cond_init(&lib.not_full, NULL);
    pthread_cond_init(&lib.not_empty, NULL);
    pthread_cond_init(&lib.finished_cond, NULL);
}

/* Destroy mutex and condition variables after all threads have been joined. */
void library_destroy(void)
{
    pthread_mutex_destroy(&lib.lock);
    pthread_cond_destroy(&lib.not_full);
    pthread_cond_destroy(&lib.not_empty);
    pthread_cond_destroy(&lib.finished_cond);
}

void *producer(void *arg)
{
    (void)arg;  /* unused */

    for (int i = 1; i <= TOTAL_RESERVATIONS; i++) {
        sleep(1);

        Reservation r;
        r.reservation_id = i;
        r.book_id = (rand() % 20) + 1;   /* book IDs from 1 to 20 */

        /* Lock before touching any shared data. */
        pthread_mutex_lock(&lib.lock);

        /* Print once if we are about to block on a full queue. */
        if (lib.count == QUEUE_CAPACITY) {
            printf("[Student] Queue FULL (%d/%d). Reservation #%d (book %d) is waiting...\n",
                   lib.count, QUEUE_CAPACITY, r.reservation_id, r.book_id);
        }

        while (lib.count == QUEUE_CAPACITY) {
            pthread_cond_wait(&lib.not_full, &lib.lock);
        }

        /* Insert at tail of the circular buffer. */
        lib.buffer[lib.tail] = r;
        lib.tail = (lib.tail + 1) % QUEUE_CAPACITY;
        lib.count++;
        lib.submitted++;

        printf("[Student] Submitted reservation #%d for book %d (queue %d/%d)\n",
               r.reservation_id, r.book_id, lib.count, QUEUE_CAPACITY);

        /* Tell a waiting consumer that the queue is no longer empty. */
        pthread_cond_signal(&lib.not_empty);

        pthread_mutex_unlock(&lib.lock);
    }

    return NULL;
}

void *consumer(void *arg)
{
    (void)arg;  /* unused */

    for (int i = 0; i < TOTAL_RESERVATIONS; i++) {
        Reservation r;

        /* Lock before touching any shared data. */
        pthread_mutex_lock(&lib.lock);

        /* Print once if we are about to block on an empty queue. */
        if (lib.count == 0) {
            printf("[Staff]   Queue EMPTY. Waiting for reservations...\n");
        }

        while (lib.count == 0) {
            pthread_cond_wait(&lib.not_empty, &lib.lock);
        }

        /* Remove from head of the circular buffer. */
        r = lib.buffer[lib.head];
        lib.head = (lib.head + 1) % QUEUE_CAPACITY;
        lib.count--;

        /* Tell a waiting producer that there is space again. */
        pthread_cond_signal(&lib.not_full);
        pthread_mutex_unlock(&lib.lock);

        printf("[Staff]   Processing reservation #%d for book %d...\n",
               r.reservation_id, r.book_id);
        sleep(3);

        /* Even book IDs are available (approved), odd are unavailable (rejected). */
        int available = (r.book_id % 2 == 0);

        /* Lock again to update the shared counters. */
        pthread_mutex_lock(&lib.lock);

        lib.processed++;
        if (available) {
            lib.approved++;
            printf("[Staff]   Reservation #%d APPROVED (book %d available)\n",
                   r.reservation_id, r.book_id);
        } else {
            lib.rejected++;
            printf("[Staff]   Reservation #%d REJECTED (book %d unavailable)\n",
                   r.reservation_id, r.book_id);
        }

        if (lib.processed == TOTAL_RESERVATIONS) {
            lib.finished = 1;
            pthread_cond_broadcast(&lib.finished_cond);
        }

        pthread_mutex_unlock(&lib.lock);
    }

    return NULL;
}

/* Print all statistics. The caller MUST already hold lib.lock. */
static void print_report(const char *title)
{
    printf("\n========== %s ==========\n", title);
    printf("  Total submitted : %d\n", lib.submitted);
    printf("  Total processed : %d\n", lib.processed);
    printf("  Approved        : %d\n", lib.approved);
    printf("  Rejected        : %d\n", lib.rejected);
    printf("  Queue size      : %d/%d\n", lib.count, QUEUE_CAPACITY);
    printf("=====================================\n\n");
}

void *monitor(void *arg)
{
    (void)arg;  /* unused */

    pthread_mutex_lock(&lib.lock);

    while (!lib.finished) {
        /* pthread_cond_timedwait needs an ABSOLUTE time: now + interval. */
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += MONITOR_INTERVAL;

        int rc = 0;
        while (!lib.finished && rc != ETIMEDOUT) {
            rc = pthread_cond_timedwait(&lib.finished_cond, &lib.lock, &deadline);
        }

        if (lib.finished) {
            break;
        }

        print_report("MONITOR REPORT");
    }

    print_report("FINAL REPORT");

    pthread_mutex_unlock(&lib.lock);
    return NULL;
}

int main(void)
{
    pthread_t student, staff, watcher;

    srand((unsigned)time(NULL));
    library_init();

    pthread_create(&student, NULL, producer, NULL);
    pthread_create(&staff, NULL, consumer, NULL);
    pthread_create(&watcher, NULL, monitor, NULL);

    /* Wait for all three threads to finish before cleaning up. */
    pthread_join(student, NULL);
    pthread_join(staff, NULL);
    pthread_join(watcher, NULL);

    library_destroy();
    printf("All reservations processed. Program finished.\n");
    return 0;
}