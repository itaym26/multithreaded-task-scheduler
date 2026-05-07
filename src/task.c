#include "task.h"
#include "error.h"
#include "logger.h"
#include <stdlib.h>

Task *task_create(int id, int priority,
                  void (*function)(void *), void *args)
{
    if (!function) {
        Error err;
        error_set(&err, ERR_NULL_POINTER, "task", "task_create",
                  "function pointer is NULL");
        error_handle(&err);
        return NULL;
    }

    Task *task = malloc(sizeof(Task));
    if (!task) {
        Error err;
        error_set(&err, ERR_MEMORY_ALLOC, "task", "task_create",
                  "failed to allocate Task");
        error_handle(&err);
        return NULL;
    }

    task->id       = id;
    task->priority = priority;
    task->function = function;
    task->args     = args;
    
    LOG_DEBUG("task", "Created  task id=%-4d priority=%d", id, priority);
    return task;
}

void task_destroy(Task *task)
{
    if (!task) return;//protect from duble free
    LOG_DEBUG("task", "Destroyed task id=%-4d", task->id);
    free(task);
}

void task_execute(Task *task)
{
    if (!task) {
        Error err;
        error_set(&err, ERR_NULL_POINTER, "task", "task_execute",
                  "task pointer is NULL");
        error_handle(&err);
        return;
    }
    if (!task->function) {
        Error err;
        error_set(&err, ERR_NULL_POINTER, "task", "task_execute",
                  "task function pointer is NULL");
        error_handle(&err);
        return;
    }

    LOG_DEBUG("task", "Executing task id=%-4d priority=%d",
              task->id, task->priority);
    task->function(task->args);
}
