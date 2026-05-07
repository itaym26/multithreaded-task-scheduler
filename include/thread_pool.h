#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include <stdint.h>
#include "queue.h"
#include "error.h"

/* ── Performance metrics ──────────────────────────────────────────────────────
 * Counters updated by worker threads after each task execution.
 * Protected by ThreadPool.metrics_mutex for thread-safe access.
 * ────────────────────────────────────────────────────────────────────────── */
typedef struct {
    uint64_t tasks_submitted;     /* total tasks submitted by producers       */
    uint64_t tasks_completed;     /* tasks successfully executed by workers    */
    uint64_t tasks_failed;        /* tasks that could not be dequeued/executed */
    double   total_exec_seconds;  /* sum of all individual task wall-clock times */
} PoolMetrics;

/* ── Thread pool ──────────────────────────────────────────────────────────────
 * Manages a fixed set of worker threads that pull tasks from a shared queue.
 * Follows the Producer-Consumer pattern:
 *   producers submit tasks via thread_pool_submit()
 *   workers loop in worker_func(), blocking on queue_dequeue() when idle
 * ────────────────────────────────────────────────────────────────────────── */
typedef struct {
    pthread_t      *threads;       /* array of worker thread handles          */
    int             thread_count;  /* number of worker threads                */
    Queue           queue;         /* the shared priority queue               */
    int             running;       /* 1 = accepting tasks, 0 = shutting down  */
    PoolMetrics     metrics;       /* execution statistics                    */
    pthread_mutex_t metrics_mutex; /* guards concurrent writes to metrics     */
} ThreadPool;

/* ── API ──────────────────────────────────────────────────────────────────── */

/*
 * thread_pool_create
 * ------------------
 * Allocates a ThreadPool, initialises its queue and metrics mutex,
 * and spawns `thread_count` worker threads, each running worker_func().
 * Workers begin waiting for tasks immediately after creation.
 *
 * Parameters:
 *   thread_count   — number of concurrent worker threads to create. Must be > 0.
 *   queue_capacity — max number of pending tasks in the queue. Must be > 0.
 *   err            — out-parameter filled on failure. May be NULL.
 *
 * Returns: pointer to the new ThreadPool on success, or NULL on failure
 *          (invalid arguments, allocation failure, or pthread_create failure).
 */
ThreadPool *thread_pool_create(int thread_count, int queue_capacity, Error *err);

/*
 * thread_pool_destroy
 * -------------------
 * Initiates a graceful shutdown:
 *   1. Sets running = 0.
 *   2. Calls queue_shutdown() to wake all blocked workers.
 *   3. Joins every worker thread (waits for it to finish its current task).
 *   4. Frees all resources (queue, mutexes, thread array, pool struct).
 * Tasks still in the queue when this is called are discarded.
 *
 * Parameters:
 *   pool — pointer to the ThreadPool to destroy.
 *
 * Returns: nothing
 */
void thread_pool_destroy(ThreadPool *pool);

/*
 * thread_pool_submit
 * ------------------
 * Submits a task to the pool by enqueuing it into the shared priority queue.
 * Blocks if the queue is full until a slot becomes available.
 * On success, ownership of the task transfers to the pool (and eventually
 * to the worker that executes it).
 *
 * Parameters:
 *   pool — pointer to the ThreadPool.
 *   task — the Task to submit. Must not be NULL.
 *   err  — out-parameter filled on failure. May be NULL.
 *
 * Returns: 0 on success, -1 on failure (pool shut down, NULL argument).
 */
int thread_pool_submit(ThreadPool *pool, Task *task, Error *err);

/*
 * thread_pool_get_metrics
 * -----------------------
 * Returns a snapshot of the current performance metrics.
 * Thread-safe: acquires metrics_mutex briefly to copy the struct.
 *
 * Parameters:
 *   pool — pointer to the ThreadPool.
 *
 * Returns: a PoolMetrics struct by value (zeroed if pool is NULL).
 */
PoolMetrics thread_pool_get_metrics(ThreadPool *pool);

/*
 * thread_pool_print_metrics
 * -------------------------
 * Prints a formatted metrics report (task counts, total time, average time)
 * to stdout. Does nothing if metrics is NULL.
 *
 * Parameters:
 *   metrics — pointer to the PoolMetrics snapshot to display.
 *
 * Returns: nothing
 */
void thread_pool_print_metrics(const PoolMetrics *metrics);

#endif /* THREAD_POOL_H */
