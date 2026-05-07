#include "thread_pool.h"
#include "task.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

/* ── Worker thread function ───────────────────────────────────────────────────
 * Each worker runs this loop for its entire lifetime.
 * It blocks on queue_dequeue() when no tasks are available, consuming zero CPU.
 * The loop exits cleanly when queue_dequeue returns NULL (shutdown signal).
 * ────────────────────────────────────────────────────────────────────────── */
static void *worker_func(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;

    LOG_INFO("worker", "Thread started (tid=%lu)",
             (unsigned long)pthread_self());

    for (;;) {
        Error err;
        error_init(&err);

        /* Block here until a task is available or shutdown is signalled */
        Task *task = queue_dequeue(&pool->queue, &err);

        if (!task) {
            /* ERR_QUEUE_SHUTDOWN is the expected clean-exit signal */
            if (err.code == ERR_QUEUE_SHUTDOWN) break;

            /* Any other NULL return is an unexpected error */
            error_handle(&err);
            pthread_mutex_lock(&pool->metrics_mutex);
            pool->metrics.tasks_failed++;
            pthread_mutex_unlock(&pool->metrics_mutex);
            continue;
        }

        /* Measure wall-clock time for this task using a monotonic clock
           (CLOCK_MONOTONIC is not affected by system time adjustments) */
        struct timespec t_start, t_end;
        clock_gettime(CLOCK_MONOTONIC, &t_start);

        task_execute(task);

        clock_gettime(CLOCK_MONOTONIC, &t_end);

        /* Convert timespec difference to seconds as a double */
        double elapsed = (double)(t_end.tv_sec  - t_start.tv_sec) +
                         (double)(t_end.tv_nsec - t_start.tv_nsec) * 1e-9;

        /* Worker owns the task after dequeue — free it now that it is done */
        task_destroy(task);

        /* Update shared metrics under the metrics mutex to avoid data races */
        pthread_mutex_lock(&pool->metrics_mutex);
        pool->metrics.tasks_completed++;
        pool->metrics.total_exec_seconds += elapsed;
        pthread_mutex_unlock(&pool->metrics_mutex);
    }

    LOG_INFO("worker", "Thread exiting (tid=%lu)",
             (unsigned long)pthread_self());
    return NULL;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

ThreadPool *thread_pool_create(int thread_count, int queue_capacity, Error *err)
{
    if (thread_count <= 0 || queue_capacity <= 0) {
        if (err) error_set(err, ERR_INVALID_ARG, "thread_pool",
                           "thread_pool_create",
                           "thread_count and queue_capacity must be > 0");
        return NULL;
    }

    ThreadPool *pool = malloc(sizeof(ThreadPool));
    if (!pool) {
        if (err) error_set(err, ERR_MEMORY_ALLOC, "thread_pool",
                           "thread_pool_create",
                           "failed to allocate ThreadPool");
        return NULL;
    }
    memset(pool, 0, sizeof(*pool));

    /* Initialise the shared priority queue */
    Error q_err;
    error_init(&q_err);
    if (queue_init(&pool->queue, queue_capacity, &q_err) != 0) {
        if (err) *err = q_err;
        else     error_handle(&q_err);
        free(pool);
        return NULL;
    }

    /* Create the mutex that protects the metrics counters */
    if (pthread_mutex_init(&pool->metrics_mutex, NULL) != 0) {
        if (err) error_set(err, ERR_MUTEX_INIT, "thread_pool",
                           "thread_pool_create",
                           "failed to init metrics_mutex");
        queue_destroy(&pool->queue);
        free(pool);
        return NULL;
    }

    /* Allocate the array that will hold each thread's handle (pthread_t) */
    pool->threads = malloc((size_t)thread_count * sizeof(pthread_t));
    if (!pool->threads) {
        if (err) error_set(err, ERR_MEMORY_ALLOC, "thread_pool",
                           "thread_pool_create",
                           "failed to allocate thread array");
        pthread_mutex_destroy(&pool->metrics_mutex);
        queue_destroy(&pool->queue);
        free(pool);
        return NULL;
    }

    pool->thread_count = thread_count;
    pool->running      = 1;

    /* Spawn each worker thread — they start executing worker_func immediately */
    for (int i = 0; i < thread_count; i++) {
        int rc = pthread_create(&pool->threads[i], NULL, worker_func, pool);
        if (rc != 0) {
            if (err) error_set(err, ERR_THREAD_CREATE, "thread_pool",
                               "thread_pool_create",
                               "pthread_create failed");
            /* Clean up the threads that were already spawned before failing */
            pool->thread_count = i;
            thread_pool_destroy(pool);
            return NULL;
        }
        LOG_DEBUG("thread_pool", "Spawned worker %d", i);
    }

    LOG_INFO("thread_pool",
             "Created pool: %d threads, queue capacity=%d",
             thread_count, queue_capacity);
    return pool;
}

void thread_pool_destroy(ThreadPool *pool)
{
    if (!pool) return;

    LOG_INFO("thread_pool", "Initiating graceful shutdown...");

    pool->running = 0;

    /* Wake all blocked workers so they can see the shutdown flag and exit */
    queue_shutdown(&pool->queue);

    /* Wait for every worker to finish its current task and exit cleanly */
    for (int i = 0; i < pool->thread_count; i++) {
        int rc = pthread_join(pool->threads[i], NULL);
        if (rc != 0) {
            Error err;
            error_set(&err, ERR_THREAD_JOIN, "thread_pool",
                      "thread_pool_destroy", "pthread_join failed");
            error_handle(&err);
        }
        LOG_DEBUG("thread_pool", "Joined worker %d", i);
    }

    /* Release all resources in reverse initialisation order */
    queue_destroy(&pool->queue);
    pthread_mutex_destroy(&pool->metrics_mutex);
    free(pool->threads);
    free(pool);

    LOG_INFO("thread_pool", "Pool destroyed");
}

int thread_pool_submit(ThreadPool *pool, Task *task, Error *err)
{
    if (!pool || !task) {
        if (err) error_set(err, ERR_NULL_POINTER, "thread_pool",
                           "thread_pool_submit",
                           "pool or task is NULL");
        return -1;
    }
    if (!pool->running) {
        if (err) error_set(err, ERR_POOL_SHUTDOWN, "thread_pool",
                           "thread_pool_submit",
                           "pool is not running");
        return -1;
    }

    Error q_err;
    error_init(&q_err);

    if (queue_enqueue(&pool->queue, task, &q_err) != 0) {
        if (err) *err = q_err;
        else     error_handle(&q_err);
        return -1;
    }

    /* Count submissions separately from completions so we can detect drops */
    pthread_mutex_lock(&pool->metrics_mutex);
    pool->metrics.tasks_submitted++;
    pthread_mutex_unlock(&pool->metrics_mutex);

    LOG_DEBUG("thread_pool", "Submitted task id=%-4d priority=%d",
              task->id, task->priority);
    return 0;
}

PoolMetrics thread_pool_get_metrics(ThreadPool *pool)
{
    PoolMetrics snap = {0};
    if (!pool) return snap;

    /* Copy the entire struct under the lock to guarantee a consistent snapshot */
    pthread_mutex_lock(&pool->metrics_mutex);
    snap = pool->metrics;
    pthread_mutex_unlock(&pool->metrics_mutex);
    return snap;
}

void thread_pool_print_metrics(const PoolMetrics *m)
{
    if (!m) return;

    /* Avoid division by zero when no tasks have completed yet */
    double avg = (m->tasks_completed > 0)
                 ? m->total_exec_seconds / (double)m->tasks_completed
                 : 0.0;

    printf("\n┌─────────────────────────────────────────┐\n");
    printf("│          Thread Pool Metrics            │\n");
    printf("├─────────────────────────────────────────┤\n");
    printf("│  Tasks submitted : %-20llu │\n", (unsigned long long)m->tasks_submitted);
    printf("│  Tasks completed : %-20llu │\n", (unsigned long long)m->tasks_completed);
    printf("│  Tasks failed    : %-20llu │\n", (unsigned long long)m->tasks_failed);
    printf("│  Total exec time : %-17.4f s │\n", m->total_exec_seconds);
    printf("│  Avg exec time   : %-17.6f s │\n", avg);
    printf("└─────────────────────────────────────────┘\n\n");
}
