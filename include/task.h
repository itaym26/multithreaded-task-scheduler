#ifndef TASK_H
#define TASK_H

/* ── Priority constants ───────────────────────────────────────────────────────
 * Convenience values for the task priority field.
 * Rule: lower number = higher scheduling priority.
 * The priority queue always dequeues the task with the lowest priority value.
 * ────────────────────────────────────────────────────────────────────────── */
#define TASK_PRIORITY_HIGH   0
#define TASK_PRIORITY_NORMAL 5
#define TASK_PRIORITY_LOW    10

/* ── Task structure ───────────────────────────────────────────────────────────
 * Represents a single unit of work: a function pointer and its arguments.
 * The function pointer accepts void* so any function signature can be wrapped,
 * and void* args allows passing any data structure without type coupling.
 * ────────────────────────────────────────────────────────────────────────── */
typedef struct {
    int    id;               /* unique identifier for this task               */
    int    priority;         /* scheduling priority — lower value = runs first */
    void (*function)(void *);/* pointer to the work function to execute        */
    void  *args;             /* pointer to the function's input data (any type) */
} Task;

/* ── API ──────────────────────────────────────────────────────────────────── */

/*
 * task_create
 * -----------
 * Allocates and initialises a new Task on the heap.
 * The caller is responsible for eventually freeing it with task_destroy.
 * Note: args is stored by pointer only — the Task does not copy the data.
 *
 * Parameters:
 *   id       — unique numeric identifier for the task.
 *   priority — scheduling priority (0 = highest, 10 = lowest).
 *   function — pointer to the function to run. Must not be NULL.
 *   args     — pointer to the function's input data. May be NULL.
 *
 * Returns: pointer to the new Task, or NULL on failure
 *          (function is NULL, or malloc failed).
 */
Task *task_create(int id, int priority, void (*function)(void *), void *args);

/*
 * task_destroy
 * ------------
 * Frees the Task struct itself.
 * Does NOT free args — the owner of args is responsible for releasing it.
 * This separation is intentional: the task does not know who allocated args
 * or whether it is still needed elsewhere.
 *
 * Parameters:
 *   task — pointer to the Task to free. No-op if NULL.
 *
 * Returns: nothing
 */
void task_destroy(Task *task);

/*
 * task_execute
 * ------------
 * Calls task->function(task->args).
 * Does not free the task after execution — the caller must call task_destroy.
 *
 * Parameters:
 *   task — pointer to the Task to run. Reports an error and returns if NULL.
 *
 * Returns: nothing
 */
void task_execute(Task *task);

#endif /* TASK_H */
