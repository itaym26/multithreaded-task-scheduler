#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#include "error.h"
#include "logger.h"
#include "task.h"
#include "queue.h"
#include "thread_pool.h"

/* ── Configuration ────────────────────────────────────────────────────────── */
#define WORKER_THREADS   4   /* number of concurrent worker threads           */
#define QUEUE_CAPACITY   16  /* max pending tasks before producers block       */
#define TOTAL_TASKS      20  /* total tasks submitted across all producers     */
#define PRODUCER_THREADS 2   /* number of producer threads                     */

/* ── Demo task argument struct ────────────────────────────────────────────────
 * Passed as void* to task functions; cast back to TaskArgs* inside.
 * Allocated by the producer and freed by the task function itself.
 * ────────────────────────────────────────────────────────────────────────── */
typedef struct {
    int task_id;
    int workload_ms; /* simulated duration in milliseconds */
} TaskArgs;

/* ── Demo task functions ──────────────────────────────────────────────────── */

/* Simulates a CPU-bound computation by sleeping for workload_ms milliseconds */
static void demo_compute(void *arg)
{
    TaskArgs *a = (TaskArgs *)arg; /* cast void* back to the concrete type */
    LOG_INFO("task_fn", "task %d  — computing for %d ms ...",
             a->task_id, a->workload_ms);

    struct timespec ts = {
        .tv_sec  = a->workload_ms / 1000,
        .tv_nsec = (a->workload_ms % 1000) * 1000000L
    };
    nanosleep(&ts, NULL);

    LOG_INFO("task_fn", "task %d  — done", a->task_id);
    free(a); /* task function owns args and is responsible for freeing them */
}

/* Simulates an I/O-bound operation (network, disk, etc.) */
static void demo_io(void *arg)
{
    TaskArgs *a = (TaskArgs *)arg;
    LOG_INFO("task_fn", "task %d  — I/O simulation (%d ms) ...",
             a->task_id, a->workload_ms);

    struct timespec ts = {
        .tv_sec  = a->workload_ms / 1000,
        .tv_nsec = (a->workload_ms % 1000) * 1000000L
    };
    nanosleep(&ts, NULL);

    LOG_INFO("task_fn", "task %d  — I/O done", a->task_id);
    free(a);
}

/* ── Producer thread ──────────────────────────────────────────────────────────
 * Each producer creates a batch of tasks with random priorities and durations,
 * then submits them to the thread pool one by one.
 * ────────────────────────────────────────────────────────────────────────── */
typedef struct {
    ThreadPool *pool;
    int         producer_id;
    int         task_count; /* number of tasks this producer will submit */
    int         start_id;   /* ID of the first task in this batch         */
} ProducerArgs;

static void *producer_func(void *arg)
{
    ProducerArgs *pa = (ProducerArgs *)arg;

    /* Seed rand differently per thread so each producer picks different values */
    srand((unsigned)(time(NULL) ^ (uintptr_t)arg));

    for (int i = 0; i < pa->task_count; i++) {
        int id       = pa->start_id + i;
        int priority = rand() % 11;       /* 0 (highest) … 10 (lowest)  */
        int work_ms  = 50 + rand() % 200; /* 50–250 ms per task          */

        /* Allocate args on the heap — the task function will free them */
        TaskArgs *ta = malloc(sizeof(TaskArgs));
        if (!ta) {
            Error err;
            error_set(&err, ERR_MEMORY_ALLOC, "main", "producer_func",
                      "failed to allocate TaskArgs");
            error_handle(&err);
            continue;
        }
        ta->task_id     = id;
        ta->workload_ms = work_ms;

        /* Alternate between I/O and compute tasks to simulate a mixed workload */
        void (*fn)(void *) = (i % 3 == 0) ? demo_io : demo_compute;

        Task *task = task_create(id, priority, fn, ta);
        if (!task) {
            free(ta); /* task_create failed — we still own ta, so free it */
            continue;
        }

        Error err;
        error_init(&err);

        if (thread_pool_submit(pa->pool, task, &err) != 0) {
            error_handle(&err);
            task_destroy(task); /* frees the Task struct only */
            free(ta);           /* producer still owns ta — free it here */
        }

        LOG_INFO("producer", "Producer %d submitted task %d (priority=%d)",
                 pa->producer_id, id, priority);

        /* Pace the producer so it does not flood the queue instantly */
        struct timespec pace = {.tv_sec = 0, .tv_nsec = 20000000L}; /* 20 ms */
        nanosleep(&pace, NULL);
    }

    LOG_INFO("producer", "Producer %d finished", pa->producer_id);
    return NULL;
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

int main(void)
{
    /* Initialise the global logger before any other module uses it */
    if (logger_init(&g_logger, LOG_INFO, stdout) != 0) {
        fprintf(stderr, "FATAL: failed to initialise logger\n");
        return EXIT_FAILURE;
    }

    LOG_INFO("main", "=== Multithreaded Task Scheduler ===");
    LOG_INFO("main", "Workers=%d  Queue capacity=%d  Tasks=%d",
             WORKER_THREADS, QUEUE_CAPACITY, TOTAL_TASKS);

    /* Create the thread pool — this also spawns the worker threads */
    Error err;
    error_init(&err);

    ThreadPool *pool = thread_pool_create(WORKER_THREADS, QUEUE_CAPACITY, &err);
    if (!pool) {
        error_handle(&err);
        logger_destroy(&g_logger);
        return EXIT_FAILURE;
    }

    /* Divide the total task count evenly across producer threads */
    pthread_t    producers[PRODUCER_THREADS];
    ProducerArgs p_args[PRODUCER_THREADS];
    int tasks_per_producer = TOTAL_TASKS / PRODUCER_THREADS;

    for (int i = 0; i < PRODUCER_THREADS; i++) {
        p_args[i].pool        = pool;
        p_args[i].producer_id = i + 1;
        /* Give the last producer any remainder tasks from integer division */
        p_args[i].task_count  = (i == PRODUCER_THREADS - 1)
                                ? TOTAL_TASKS - i * tasks_per_producer
                                : tasks_per_producer;
        p_args[i].start_id    = i * tasks_per_producer + 1;

        int rc = pthread_create(&producers[i], NULL, producer_func, &p_args[i]);
        if (rc != 0) {
            error_set(&err, ERR_THREAD_CREATE, "main", "main",
                      "failed to create producer thread");
            error_handle(&err);
        }
    }

    /* Wait for all producers to finish submitting their tasks */
    for (int i = 0; i < PRODUCER_THREADS; i++) {
        pthread_join(producers[i], NULL);
    }
    LOG_INFO("main", "All producers finished — waiting for workers to drain queue ...");

    /* Poll until the queue is empty before shutting down.
       This ensures all submitted tasks are executed, not discarded. */
    while (queue_size(&pool->queue) > 0) {
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 10000000L}; /* 10 ms */
        nanosleep(&ts, NULL);
    }

    /* Brief pause to let workers finish the last task they already dequeued */
    struct timespec settle = {.tv_sec = 0, .tv_nsec = 100000000L}; /* 100 ms */
    nanosleep(&settle, NULL);

    /* Print metrics before destroying the pool (destruction discards them) */
    PoolMetrics metrics = thread_pool_get_metrics(pool);
    thread_pool_print_metrics(&metrics);

    /* Graceful shutdown: signals workers, joins them, frees all memory */
    thread_pool_destroy(pool);

    LOG_INFO("main", "Scheduler shut down cleanly. Goodbye.");
    logger_destroy(&g_logger);

    return EXIT_SUCCESS;
}
