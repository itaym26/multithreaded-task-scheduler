#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include "task.h"
#include "error.h"

/* ── Thread-safe Priority Queue (binary min-heap) ─────────────────────────────
 *
 * Data structure:
 *   Tasks are stored in a flat array interpreted as a binary min-heap.
 *   The task with the lowest priority value is always at index 0 (the root),
 *   so dequeue always returns the highest-priority task in O(log n).
 *   Heap indices follow the standard formula:
 *     parent(i) = (i-1)/2,  left(i) = 2i+1,  right(i) = 2i+2
 *
 * Synchronisation model:
 *   mutex     — guards all access to the heap array, size, and shutdown flag.
 *   not_empty — consumer threads wait on this when the queue is empty;
 *               signalled by enqueue when a task is added.
 *   not_full  — producer threads wait on this when the queue is at capacity;
 *               signalled by dequeue when a slot is freed.
 *   shutdown  — set to 1 by queue_shutdown(); broadcast on both condition
 *               variables to unblock all waiting threads cleanly.
 * ────────────────────────────────────────────────────────────────────────── */
typedef struct {
    Task          **heap;      /* flat array of task pointers (the min-heap)  */
    int             size;      /* current number of tasks in the queue         */
    int             capacity;  /* maximum number of tasks (fixed at init time) */
    int             shutdown;  /* shutdown flag: 1 = wake all blocked threads  */
    pthread_mutex_t mutex;     /* mutual exclusion for the entire struct        */
    pthread_cond_t  not_empty; /* signalled when a task is added to the queue  */
    pthread_cond_t  not_full;  /* signalled when a task is removed             */
} Queue;

/* ── API ──────────────────────────────────────────────────────────────────── */

/*
 * queue_init
 * ----------
 * Allocates the heap array and initialises the mutex and condition variables.
 * Must be called once before any enqueue/dequeue operations.
 *
 * Parameters:
 *   queue    — pointer to the Queue struct to initialise.
 *   capacity — maximum number of pending tasks. Must be > 0.
 *   err      — out-parameter filled on failure. May be NULL.
 *
 * Returns: 0 on success, -1 on failure (details in err if provided).
 */
int queue_init(Queue *queue, int capacity, Error *err);

/*
 * queue_destroy
 * -------------
 * Drains any remaining tasks (calling task_destroy on each), then destroys
 * the mutex, condition variables, and frees the heap array.
 * NOT safe to call while threads are still blocked on the queue —
 * call queue_shutdown() first so all blocked threads can exit.
 *
 * Parameters:
 *   queue — pointer to the Queue to destroy.
 *
 * Returns: nothing
 */
void queue_destroy(Queue *queue);

/*
 * queue_enqueue
 * -------------
 * Inserts a task into the queue according to its priority.
 * Blocks the caller when the queue is full, until a slot is freed or
 * the queue shuts down.
 * Thread-safe: may be called concurrently from multiple producer threads.
 * On success, ownership of the task transfers to the queue.
 *
 * Parameters:
 *   queue — pointer to the Queue.
 *   task  — the Task to insert. Must not be NULL.
 *   err   — out-parameter filled on failure. May be NULL.
 *
 * Returns: 0 on success, -1 on failure (queue shut down, NULL argument).
 */
int queue_enqueue(Queue *queue, Task *task, Error *err);

/*
 * queue_dequeue
 * -------------
 * Removes and returns the highest-priority task (lowest priority value).
 * Blocks the caller when the queue is empty, until a task is added or
 * the queue shuts down.
 * Thread-safe: may be called concurrently from multiple consumer threads.
 * On success, ownership of the returned task transfers to the caller,
 * who must eventually call task_destroy().
 *
 * Parameters:
 *   queue — pointer to the Queue.
 *   err   — out-parameter filled on failure. May be NULL.
 *
 * Returns: pointer to the highest-priority Task, or NULL if the queue shut
 *          down and is now empty (err will contain ERR_QUEUE_SHUTDOWN).
 */
Task *queue_dequeue(Queue *queue, Error *err);

/*
 * queue_shutdown
 * --------------
 * Sets the shutdown flag and broadcasts on both condition variables,
 * waking all threads currently blocked in enqueue or dequeue.
 * After this call: enqueue will fail, dequeue will drain remaining tasks
 * and then return NULL.
 * Must be called before queue_destroy to avoid a deadlock.
 *
 * Parameters:
 *   queue — pointer to the Queue.
 *
 * Returns: nothing
 */
void queue_shutdown(Queue *queue);

/*
 * queue_size
 * ----------
 * Returns the current number of tasks in the queue.
 * Thread-safe: acquires the mutex briefly to read the size.
 *
 * Parameters:
 *   queue — pointer to the Queue.
 *
 * Returns: current task count, or 0 if queue is NULL.
 */
int queue_size(Queue *queue);

#endif /* QUEUE_H */
