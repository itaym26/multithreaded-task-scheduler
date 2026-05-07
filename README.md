# ⚙️ Multithreaded Task Scheduler in C

> A high-performance, production-quality task scheduling system built in C using POSIX threads.  
> Implements the **Producer-Consumer** pattern with a **priority queue** backed by a binary min-heap,  
> structured error reporting, thread-safe logging, and execution metrics.

<br>

[![Language](https://img.shields.io/badge/Language-C11-blue.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![Threads](https://img.shields.io/badge/Threads-POSIX%20pthreads-green.svg)](https://man7.org/linux/man-pages/man7/pthreads.7.html)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20WSL-lightgrey.svg)]()
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)]()

---

## 📋 Table of Contents

- [Overview](#-overview)
- [System Architecture](#-system-architecture)
- [Module Breakdown](#-module-breakdown)
- [Data Structures](#-data-structures)
- [Synchronisation Model](#-synchronisation-model)
- [Thread Lifecycle](#-thread-lifecycle)
- [Error Handling](#-error-handling)
- [Sample Output](#-sample-output)
- [Performance Metrics](#-performance-metrics)
- [Build & Run](#-build--run)
- [Project Structure](#-project-structure)
- [Design Decisions](#-design-decisions)

---

## 🔍 Overview

This project simulates a lightweight **operating system scheduler** component.  
Multiple producer threads generate tasks with different priorities and durations.  
A fixed pool of worker threads picks up tasks from a shared priority queue and executes them concurrently.

**Core concepts demonstrated:**
- Low-level concurrency in C (pthreads)
- Mutex and condition variable synchronisation
- Binary min-heap data structure
- Producer-Consumer design pattern
- Structured error propagation
- Safe memory management (zero leaks)

---

## 🏗 System Architecture

```
╔══════════════════════════════════════════════════════════════════════╗
║                        main.c  (Scheduler)                          ║
║                                                                      ║
║   ┌─────────────────┐              ┌──────────────────────────────┐  ║
║   │  Producer       │              │       Thread Pool            │  ║
║   │  Thread  × 2    │──submit()───►│                              │  ║
║   │                 │              │  ┌────────────────────────┐  │  ║
║   │  creates Tasks  │              │  │   Priority Queue       │  │  ║
║   │  with random    │              │  │   (binary min-heap)    │  │  ║
║   │  priority &     │              │  │                        │  │  ║
║   │  duration       │              │  │  [2][3][5][7][8][10]  │  │  ║
║   └─────────────────┘              │  │   ↑                    │  │  ║
║                                    │  │  root = highest prio   │  │  ║
║                                    │  └──────────┬─────────────┘  │  ║
║                                    │             │ dequeue()       │  ║
║                                    │  ┌──────────▼─────────────┐  │  ║
║                                    │  │   Worker Thread  × 4   │  │  ║
║                                    │  │                        │  │  ║
║                                    │  │  execute → metrics     │  │  ║
║                                    │  │  destroy → next task   │  │  ║
║                                    │  └────────────────────────┘  │  ║
║                                    └──────────────────────────────┘  ║
╚══════════════════════════════════════════════════════════════════════╝
```

---

## 📦 Module Breakdown

```
┌─────────────────────────────────────────────────────────────┐
│                     Module Dependency Graph                  │
│                                                             │
│   main.c                                                    │
│     │                                                       │
│     ├──► thread_pool.h ──► queue.h ──► task.h              │
│     │         │                │          │                 │
│     │         └────────────────┴──► error.h                │
│     │                                     │                 │
│     └──► logger.h ◄─────────────────────-┘                 │
│                                                             │
│  (all modules report errors and log through error + logger) │
└─────────────────────────────────────────────────────────────┘
```

| Module | Files | Responsibility |
|--------|-------|----------------|
| **Error** | `error.h` / `error.c` | Centralised structured error reporting |
| **Logger** | `logger.h` / `logger.c` | Thread-safe levelled logger with timestamps |
| **Task** | `task.h` / `task.c` | Unit of work: function pointer + priority + args |
| **Queue** | `queue.h` / `queue.c` | Thread-safe priority queue (binary min-heap) |
| **Thread Pool** | `thread_pool.h` / `thread_pool.c` | Worker threads, metrics, graceful shutdown |
| **Main** | `main.c` | Producer threads, system lifecycle |

---

## 🔢 Data Structures

### Priority Queue — Binary Min-Heap

Tasks are stored in a flat array interpreted as a binary min-heap.  
**Lower priority value = higher scheduling priority.**  
The task with value `0` always runs before a task with value `10`.

```
  Insertion order:  priority 7, 3, 9, 1, 5
  
  After all insertions — heap array: [1, 3, 9, 7, 5]

              1          ← root: always the highest priority task
            /   \
           3     9
          / \
         7   5

  Heap indices:
    parent(i)     = (i - 1) / 2
    left_child(i) = 2 * i + 1
    right_child(i)= 2 * i + 2
```

#### Enqueue (sift-up) — O(log n)

```
  Insert priority=2 at the end:
  
  [1, 3, 9, 7, 5, 2]
                  ↑ new element

  Step 1: compare with parent(5) = index 2 → value 9
          2 < 9 → swap
  [1, 3, 2, 7, 5, 9]

  Step 2: compare with parent(2) = index 0 → value 1
          2 > 1 → stop ✓

  Final heap:
              1
            /   \
           3     2
          / \   /
         7   5 9
```

#### Dequeue (sift-down) — O(log n)

```
  Remove root (priority=1):

  Step 1: replace root with last element
  [5, 3, 2, 7, 9]  ← 5 is now at root

  Step 2: sift down — compare 5 with children 3 and 2
          smallest child = 2 → swap
  [2, 3, 5, 7, 9]

  Step 3: compare 5 with children (none) → stop ✓

  Final heap:
              2
            /   \
           3     5
          / \
         7   9
```

---

## 🔒 Synchronisation Model

### Mutex + Two Condition Variables

```
┌─────────────────────────────────────────────────────────────────┐
│                        Queue (shared)                           │
│                                                                 │
│   pthread_mutex_t  mutex      ← guards ALL access to the queue │
│   pthread_cond_t   not_empty  ← workers sleep here when empty  │
│   pthread_cond_t   not_full   ← producers sleep here when full │
│   int              shutdown   ← set to 1 on shutdown           │
└─────────────────────────────────────────────────────────────────┘

  PRODUCER                         CONSUMER (worker)
     │                                    │
     │ queue_enqueue()                    │ queue_dequeue()
     │                                    │
     ▼                                    ▼
  lock mutex                           lock mutex
     │                                    │
  size >= capacity?                    size == 0?
   YES → cond_wait(not_full)  ───┐      YES → cond_wait(not_empty) ──┐
   NO  → add to heap             │      NO  → remove from heap        │
         signal(not_empty) ──────┼────────────────────────────────►  │
         unlock                  │            signal(not_full) ───────┼──►
                                 │            unlock                  │
                                 │                                    │
                          worker signals ◄──────────────────────────-┘
                          producer wakes
```

### Shutdown Sequence

```
  main thread calls thread_pool_destroy()
        │
        ▼
  pool->running = 0
        │
        ▼
  queue_shutdown()
    ├── shutdown flag = 1
    ├── broadcast(not_empty)  ← wakes ALL sleeping workers
    └── broadcast(not_full)   ← wakes ALL sleeping producers
        │
        ▼
  pthread_join(worker_0) ──► waits for worker to finish current task
  pthread_join(worker_1) ──► waits...
  pthread_join(worker_2) ──► waits...
  pthread_join(worker_3) ──► waits...
        │
        ▼
  queue_destroy()
  free all memory
```

---

## 🔄 Thread Lifecycle

### Worker Thread

```
  pthread_create()
        │
        ▼
  ┌─────────────────────────────────────────────────────┐
  │                   for(;;) loop                      │
  │                                                     │
  │   queue_dequeue()                                   │
  │        │                                            │
  │        ├── queue empty? ──► cond_wait(not_empty)    │
  │        │                        │                   │
  │        │   ◄── producer signals ┘                   │
  │        │                                            │
  │        ├── shutdown + empty? ──► break ──────────┐  │
  │        │                                         │  │
  │        ▼                                         │  │
  │   clock_gettime(start)                           │  │
  │        │                                         │  │
  │   task_execute()          ← runs the function    │  │
  │        │                                         │  │
  │   clock_gettime(end)                             │  │
  │        │                                         │  │
  │   task_destroy()          ← worker owns task     │  │
  │        │                                         │  │
  │   update metrics          ← under mutex          │  │
  │        │                                         │  │
  │   ◄────┘  (next iteration)                       │  │
  │                                                  │  │
  └──────────────────────────────────────────────────┼──┘
                                                     │
                                                     ▼
                                               return NULL
                                                     │
                                                     ▼
                                         pthread_join() returns in main
```

### Producer Thread

```
  pthread_create()
        │
        ▼
  for i in range(task_count):
        │
        ├── malloc(TaskArgs)
        ├── task_create(id, random_priority, fn, args)
        │
        ├── thread_pool_submit()
        │        │
        │        ├── queue full? ──► cond_wait(not_full)
        │        │                       │
        │        │   ◄── worker signals ─┘
        │        │
        │        └── enqueue → signal(not_empty) → worker wakes
        │
        ├── nanosleep(20ms)   ← pacing: don't flood the queue
        │
        └── (next task)
        │
        ▼
  return NULL  ──► pthread_join() returns in main
```

---

## ⚠️ Error Handling

Every function that can fail writes to an `Error *` out-parameter before returning.  
Errors are **never silent** — they are always reported through `error_handle()`.

### Error Record Format

```json
{
  "error_code": 101,
  "error_name": "ERR_MUTEX_LOCK",
  "message":    "Failed to lock mutex",
  "module":     "queue",
  "context":    "enqueue operation",
  "timestamp":  "2025-06-15 14:32:01"
}
```

### Error Code Reference

```
┌──────────────────────────────────────────────────────────┐
│                    Error Code Table                      │
├──────────┬────────────────────────┬─────────────────────┤
│  Code    │  Name                  │  Category           │
├──────────┼────────────────────────┼─────────────────────┤
│  0       │  ERR_NONE              │  No error           │
├──────────┼────────────────────────┼─────────────────────┤
│  100     │  ERR_MUTEX_INIT        │  Synchronisation    │
│  101     │  ERR_MUTEX_LOCK        │                     │
│  102     │  ERR_MUTEX_UNLOCK      │                     │
│  103     │  ERR_COND_INIT         │                     │
│  104     │  ERR_COND_WAIT         │                     │
│  105     │  ERR_COND_SIGNAL       │                     │
│  106     │  ERR_COND_BROADCAST    │                     │
├──────────┼────────────────────────┼─────────────────────┤
│  201     │  ERR_MEMORY_ALLOC      │  Memory             │
├──────────┼────────────────────────┼─────────────────────┤
│  301     │  ERR_QUEUE_FULL        │  Queue              │
│  302     │  ERR_QUEUE_EMPTY       │                     │
│  303     │  ERR_QUEUE_SHUTDOWN    │                     │
├──────────┼────────────────────────┼─────────────────────┤
│  401     │  ERR_THREAD_CREATE     │  Thread Pool        │
│  402     │  ERR_THREAD_JOIN       │                     │
│  403     │  ERR_POOL_SHUTDOWN     │                     │
├──────────┼────────────────────────┼─────────────────────┤
│  501     │  ERR_NULL_POINTER      │  General            │
│  502     │  ERR_INVALID_ARG       │                     │
└──────────┴────────────────────────┴─────────────────────┘
```

### Error Propagation Flow

```
  queue_enqueue()
       │
       ├── pthread_mutex_lock() fails
       │        │
       │        └── error_set(err, ERR_MUTEX_LOCK, "queue", "enqueue", "...")
       │                 │
       │                 └── return -1
       │
  thread_pool_submit()
       │
       ├── receives -1 from queue_enqueue()
       │        │
       │        └── *err = q_err  (propagates the error up)
       │                 │
       │                 └── return -1
       │
  producer_func()
       │
       ├── receives -1 from submit()
       │        │
       │        └── error_handle(&err)  ← prints JSON to stderr
       │                 │
       │                 └── continues (task is discarded)
```

---

## 🖥 Sample Output

```
[14:05:01] [INFO ] [main        ] === Multithreaded Task Scheduler ===
[14:05:01] [INFO ] [main        ] Workers=4  Queue capacity=16  Tasks=20
[14:05:01] [INFO ] [queue       ] Initialised (capacity=16)
[14:05:01] [INFO ] [worker      ] Thread started (tid=139785624516352)
[14:05:01] [INFO ] [worker      ] Thread started (tid=139785616123648)
[14:05:01] [INFO ] [worker      ] Thread started (tid=139785607730944)
[14:05:01] [INFO ] [worker      ] Thread started (tid=139785599338240)
[14:05:01] [INFO ] [thread_pool ] Created pool: 4 threads, queue capacity=16
[14:05:01] [INFO ] [producer    ] Producer 1 submitted task 1  (priority=7)
[14:05:01] [INFO ] [producer    ] Producer 2 submitted task 11 (priority=2)
[14:05:01] [INFO ] [task_fn     ] task 11 — computing for 143 ms ...   ← priority 2 runs first!
[14:05:01] [INFO ] [task_fn     ] task 1  — I/O simulation (97 ms) ...
[14:05:01] [INFO ] [producer    ] Producer 1 submitted task 2  (priority=0)
[14:05:01] [INFO ] [task_fn     ] task 2  — computing for 201 ms ...   ← priority 0 (highest) runs immediately
[14:05:01] [INFO ] [producer    ] Producer 2 submitted task 12 (priority=9)
...
[14:05:03] [INFO ] [task_fn     ] task 9  — done
[14:05:03] [INFO ] [main        ] All producers finished — waiting for workers to drain queue ...

┌─────────────────────────────────────────┐
│          Thread Pool Metrics            │
├─────────────────────────────────────────┤
│  Tasks submitted : 20                   │
│  Tasks completed : 20                   │
│  Tasks failed    : 0                    │
│  Total exec time :              2.9412 s│
│  Avg exec time   :           0.147060 s │
└─────────────────────────────────────────┘

[14:05:03] [INFO ] [thread_pool ] Initiating graceful shutdown...
[14:05:03] [INFO ] [queue       ] Shutdown signalled
[14:05:03] [INFO ] [worker      ] Thread exiting (tid=139785624516352)
[14:05:03] [INFO ] [worker      ] Thread exiting (tid=139785616123648)
[14:05:03] [INFO ] [worker      ] Thread exiting (tid=139785607730944)
[14:05:03] [INFO ] [worker      ] Thread exiting (tid=139785599338240)
[14:05:03] [INFO ] [queue       ] Destroyed
[14:05:03] [INFO ] [thread_pool ] Pool destroyed
[14:05:03] [INFO ] [main        ] Scheduler shut down cleanly. Goodbye.
```

### Priority in Action

Notice that tasks with lower priority values execute before tasks with higher values,  
regardless of submission order:

```
  Submitted order:   task(p=7) → task(p=2) → task(p=0) → task(p=9)

  Execution order:   task(p=0) → task(p=2) → task(p=7) → task(p=9)
                      ↑ highest priority runs first
```

---

## 📊 Performance Metrics

Metrics are collected per-task using `CLOCK_MONOTONIC` (not affected by system clock changes):

```
  For each task:
    t_start = clock_gettime(CLOCK_MONOTONIC)
    task_execute()
    t_end   = clock_gettime(CLOCK_MONOTONIC)
    elapsed = (t_end.tv_sec - t_start.tv_sec)
            + (t_end.tv_nsec - t_start.tv_nsec) × 10⁻⁹

  Aggregated under metrics_mutex:
    tasks_completed++
    total_exec_seconds += elapsed

  At shutdown:
    avg_exec_time = total_exec_seconds / tasks_completed
```

### Ownership Model

```
  ┌──────────────────────────────────────────────────────────┐
  │                   Memory Ownership                       │
  │                                                          │
  │  Producer                                                │
  │    │                                                     │
  │    ├── malloc(TaskArgs)  ← producer owns                 │
  │    ├── task_create()     ← producer owns                 │
  │    │                                                     │
  │    └── thread_pool_submit()                              │
  │              │                                           │
  │              │   ownership transfers to queue/pool       │
  │              ▼                                           │
  │         Worker                                           │
  │              │                                           │
  │              ├── task_execute()                          │
  │              │       └── fn(args) → free(args)  ← task  │
  │              │                      function frees args  │
  │              │                                           │
  │              └── task_destroy(task)  ← worker frees Task │
  └──────────────────────────────────────────────────────────┘
```

---

## 🛠 Build & Run

### Prerequisites

- GCC ≥ 9
- GNU Make
- pthreads (standard on Linux / WSL)

### Commands

```bash
# Debug build — AddressSanitizer + UndefinedBehaviorSanitizer enabled
make

# Release build — optimised, no sanitisers
make release

# Build and run immediately
make run

# Memory leak check with Valgrind
make valgrind

# Remove all build artifacts
make clean
```

### Changing Log Verbosity

Open `src/main.c` and change the log level:

```c
// See all internal events (enqueue, dequeue, task create/destroy)
logger_init(&g_logger, LOG_DEBUG, stdout);

// Normal mode — operational events only (default)
logger_init(&g_logger, LOG_INFO, stdout);

// Quiet mode — warnings and errors only
logger_init(&g_logger, LOG_WARN, stdout);
```

### Changing Pool Configuration

```c
// src/main.c — top of file
#define WORKER_THREADS   4   // concurrent workers
#define QUEUE_CAPACITY   16  // max pending tasks
#define TOTAL_TASKS      20  // tasks to generate
#define PRODUCER_THREADS 2   // producer threads
```

---

## 📁 Project Structure

```
multithreaded-task-scheduler/
│
├── include/                  ← public header files
│   ├── error.h               ← error codes, Error struct, API
│   ├── logger.h              ← log levels, Logger struct, macros
│   ├── task.h                ← Task struct, priority constants
│   ├── queue.h               ← thread-safe priority queue
│   └── thread_pool.h         ← ThreadPool, PoolMetrics, API
│
├── src/                      ← implementation files
│   ├── error.c               ← JSON-formatted error output
│   ├── logger.c              ← thread-safe timestamped logger
│   ├── task.c                ← task lifecycle management
│   ├── queue.c               ← binary min-heap + synchronisation
│   ├── thread_pool.c         ← worker threads + metrics
│   └── main.c                ← producers, lifecycle, demo
│
├── build/                    ← compiled object files (generated)
├── Makefile                  ← build system
└── README.md                 ← this file
```

---

## 💡 Design Decisions

### Why a Binary Min-Heap?

| Option | Enqueue | Dequeue | Chosen? |
|--------|---------|---------|---------|
| Sorted linked list | O(n) | O(1) | ✗ |
| Unsorted array | O(1) | O(n) | ✗ |
| **Binary min-heap** | **O(log n)** | **O(log n)** | **✓** |
| Fibonacci heap | O(1) amort. | O(log n) | ✗ too complex |

### Why `while` Instead of `if` Before `cond_wait`?

```c
// WRONG — susceptible to spurious wakeups
if (queue->size == 0) {
    pthread_cond_wait(&queue->not_empty, &queue->mutex);
}

// CORRECT — always re-checks the condition after waking
while (queue->size == 0 && !queue->shutdown) {
    pthread_cond_wait(&queue->not_empty, &queue->mutex);
}
```

POSIX allows `pthread_cond_wait` to return even when no signal was sent (**spurious wakeup**).  
The `while` loop re-checks the condition and goes back to sleep if it is not yet satisfied.

### Why a Separate `metrics_mutex`?

Using the queue's own mutex for metrics would mean workers hold the queue lock  
while updating counters — blocking producers from enqueuing during that time.  
A dedicated `metrics_mutex` keeps metric updates independent from queue operations.

### Why `CLOCK_MONOTONIC` for Timing?

`CLOCK_REALTIME` can jump forward or backward (NTP adjustments, DST).  
`CLOCK_MONOTONIC` only moves forward and is immune to system clock changes — giving accurate elapsed-time measurements.

---

## 🔮 Possible Extensions

| Feature | Where to add |
|---------|-------------|
| Persistent log file | Add a file-sink option to `logger.c` |
| Task cancellation | Add `task->cancelled` flag; check in `worker_func` |
| Dynamic pool resize | Add `thread_pool_resize()` to `thread_pool.c` |
| Task timeout | Use `pthread_cond_timedwait` in `queue_dequeue` |
| Task result / future | Add `sem_t` + result pointer field to `Task` |
| Task dependencies | Add a dependency list to `Task`; enqueue only when deps done |

---

## 👤 Author

**Itay Matana**  
GitHub: [@itaym26](https://github.com/itaym26)

---

*Built as a systems programming portfolio project demonstrating concurrency, synchronisation, and memory management in C.*
