#include "queue.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

/* ── Internal min-heap helpers ────────────────────────────────────────────────
 * The heap is stored as a flat array where for any element at index i:
 *   parent  = (i - 1) / 2
 *   left    = 2 * i + 1
 *   right   = 2 * i + 2
 * The min-heap invariant: every parent has priority <= both its children,
 * so the task with the lowest priority value is always at index 0.
 * ────────────────────────────────────────────────────────────────────────── */

/* Swap two task pointers in the heap array */
static inline void heap_swap(Queue *q, int i, int j)
{
    Task *tmp   = q->heap[i];
    q->heap[i]  = q->heap[j];
    q->heap[j]  = tmp;
}

/* After inserting at the end, bubble the element up until the
   heap invariant is restored (parent.priority <= child.priority) */
static void heap_sift_up(Queue *q, int i)
{
    while (i > 0) {
        int parent = (i - 1) / 2;
        /* Stop as soon as the parent has equal or higher priority */
        if (q->heap[parent]->priority <= q->heap[i]->priority) break;
        heap_swap(q, parent, i);
        i = parent; /* continue checking from the parent's position */
    }
}

/* After replacing the root with the last element, push it down until
   the heap invariant is restored */
static void heap_sift_down(Queue *q, int i)
{
    int n = q->size;
    for (;;) {
        int smallest = i;
        int left     = 2 * i + 1;
        int right    = 2 * i + 2;

        /* Find which of the three nodes (current, left child, right child)
           has the smallest priority value */
        if (left  < n && q->heap[left]->priority  < q->heap[smallest]->priority)
            smallest = left;
        if (right < n && q->heap[right]->priority < q->heap[smallest]->priority)
            smallest = right;

        /* If the current node is already smallest, the heap is valid */
        if (smallest == i) break;

        heap_swap(q, i, smallest);
        i = smallest; /* continue from the position we just swapped into */
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */

int queue_init(Queue *queue, int capacity, Error *err)
{
    if (!queue || capacity <= 0) {
        if (err) error_set(err, ERR_INVALID_ARG, "queue", "queue_init",
                           "queue is NULL or capacity <= 0");
        return -1;
    }

    memset(queue, 0, sizeof(*queue));

    /* Allocate the backing array for the heap */
    queue->heap = malloc((size_t)capacity * sizeof(Task *));
    if (!queue->heap) {
        if (err) error_set(err, ERR_MEMORY_ALLOC, "queue", "queue_init",
                           "failed to allocate heap array");
        return -1;
    }

    queue->capacity = capacity;
    queue->size     = 0;
    queue->shutdown = 0;

    int rc;

    /* Create the mutex that guards all access to this struct */
    rc = pthread_mutex_init(&queue->mutex, NULL);
    if (rc != 0) {
        if (err) error_set(err, ERR_MUTEX_INIT, "queue", "queue_init",
                           "pthread_mutex_init failed");
        free(queue->heap);
        return -1;
    }

    /* not_empty: consumer threads wait here when the queue is empty */
    rc = pthread_cond_init(&queue->not_empty, NULL);
    if (rc != 0) {
        if (err) error_set(err, ERR_COND_INIT, "queue", "queue_init",
                           "pthread_cond_init (not_empty) failed");
        pthread_mutex_destroy(&queue->mutex);
        free(queue->heap);
        return -1;
    }

    /* not_full: producer threads wait here when the queue is at capacity */
    rc = pthread_cond_init(&queue->not_full, NULL);
    if (rc != 0) {
        if (err) error_set(err, ERR_COND_INIT, "queue", "queue_init",
                           "pthread_cond_init (not_full) failed");
        pthread_cond_destroy(&queue->not_empty);
        pthread_mutex_destroy(&queue->mutex);
        free(queue->heap);
        return -1;
    }

    LOG_INFO("queue", "Initialised (capacity=%d)", capacity);
    return 0;
}

void queue_destroy(Queue *queue)
{
    if (!queue) return;

    /* Drain and free any tasks left in the queue before releasing resources */
    pthread_mutex_lock(&queue->mutex);
    for (int i = 0; i < queue->size; i++) {
        task_destroy(queue->heap[i]);
    }
    queue->size = 0;
    pthread_mutex_unlock(&queue->mutex);

    /* Destroy synchronisation primitives in reverse initialisation order */
    pthread_cond_destroy(&queue->not_full);
    pthread_cond_destroy(&queue->not_empty);
    pthread_mutex_destroy(&queue->mutex);
    free(queue->heap);
    queue->heap = NULL; /* prevent accidental use after free */

    LOG_INFO("queue", "Destroyed");
}

int queue_enqueue(Queue *queue, Task *task, Error *err)
{
    if (!queue || !task) {
        if (err) error_set(err, ERR_NULL_POINTER, "queue", "queue_enqueue",
                           "queue or task is NULL");
        return -1;
    }

    pthread_mutex_lock(&queue->mutex);

    /* Block while the queue is full, unless a shutdown has been signalled.
       pthread_cond_wait atomically releases the mutex and suspends the thread;
       it re-acquires the mutex before returning. */
    while (queue->size >= queue->capacity && !queue->shutdown) {
        pthread_cond_wait(&queue->not_full, &queue->mutex);
    }

    /* If we were woken because of shutdown (not because a slot freed), abort */
    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->mutex);
        if (err) error_set(err, ERR_QUEUE_SHUTDOWN, "queue", "queue_enqueue",
                           "queue is shut down");
        return -1;
    }

    /* Place the task at the end of the array, then restore heap order */
    queue->heap[queue->size] = task;
    heap_sift_up(queue, queue->size);
    queue->size++;

    LOG_DEBUG("queue", "Enqueued task id=%-4d priority=%d  (size=%d)",
              task->id, task->priority, queue->size);

    /* Wake one waiting consumer now that a task is available */
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    return 0;
}

Task *queue_dequeue(Queue *queue, Error *err)
{
    if (!queue) {
        if (err) error_set(err, ERR_NULL_POINTER, "queue", "queue_dequeue",
                           "queue is NULL");
        return NULL;
    }

    pthread_mutex_lock(&queue->mutex);

    /* Block while the queue is empty, unless a shutdown has been signalled */
    while (queue->size == 0 && !queue->shutdown) {
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }

    /* Woken because of shutdown with no tasks remaining — signal clean exit */
    if (queue->size == 0 && queue->shutdown) {
        pthread_mutex_unlock(&queue->mutex);
        if (err) error_set(err, ERR_QUEUE_SHUTDOWN, "queue", "queue_dequeue",
                           "queue is shut down and empty");
        return NULL;
    }

    /* The root (index 0) is always the highest-priority task in a min-heap */
    Task *task = queue->heap[0];

    /* Replace root with the last element and restore the heap by sifting down */
    queue->size--;
    if (queue->size > 0) {
        queue->heap[0] = queue->heap[queue->size];
        heap_sift_down(queue, 0);
    }

    LOG_DEBUG("queue", "Dequeued  task id=%-4d priority=%d  (size=%d)",
              task->id, task->priority, queue->size);

    /* Wake one waiting producer now that a slot has been freed */
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    return task;
}

void queue_shutdown(Queue *queue)
{
    if (!queue) return;

    pthread_mutex_lock(&queue->mutex);
    queue->shutdown = 1;
    /* Broadcast (not signal) to wake ALL threads blocked on either condition */
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);

    LOG_INFO("queue", "Shutdown signalled");
}

int queue_size(Queue *queue)
{
    if (!queue) return 0;
    /* Lock briefly so we read a consistent value, not a partial update */
    pthread_mutex_lock(&queue->mutex);
    int sz = queue->size;
    pthread_mutex_unlock(&queue->mutex);
    return sz;
}
