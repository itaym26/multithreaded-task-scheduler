#ifndef ERROR_H
#define ERROR_H

#include <time.h>

/* ── Error codes ──────────────────────────────────────────────────────────────
 * Grouped by category so new codes can be added without renumbering others:
 *   100-106  synchronisation primitives (mutex / condition variable)
 *   201      memory allocation
 *   301-303  queue operations
 *   401-403  thread pool operations
 *   501-502  general / argument validation
 * ────────────────────────────────────────────────────────────────────────── */
typedef enum {
    ERR_NONE           = 0,

    /* Synchronisation */
    ERR_MUTEX_INIT     = 100,
    ERR_MUTEX_LOCK     = 101,
    ERR_MUTEX_UNLOCK   = 102,
    ERR_COND_INIT      = 103,
    ERR_COND_WAIT      = 104,
    ERR_COND_SIGNAL    = 105,
    ERR_COND_BROADCAST = 106,

    /* Memory */
    ERR_MEMORY_ALLOC   = 201,

    /* Queue */
    ERR_QUEUE_FULL     = 301,
    ERR_QUEUE_EMPTY    = 302,
    ERR_QUEUE_SHUTDOWN = 303,

    /* Thread pool */
    ERR_THREAD_CREATE  = 401,
    ERR_THREAD_JOIN    = 402,
    ERR_POOL_SHUTDOWN  = 403,

    /* General */
    ERR_NULL_POINTER   = 501,
    ERR_INVALID_ARG    = 502,
} ErrorCode;

/* ── Error record ─────────────────────────────────────────────────────────────
 * Carries full context about a failure.
 * Functions that can fail accept an (Error *err) out-parameter and fill it
 * via error_set() before returning an error code to the caller.
 * ────────────────────────────────────────────────────────────────────────── */
typedef struct {
    ErrorCode code;         /* numeric code from the ErrorCode enum            */
    char      message[256]; /* human-readable description of what went wrong   */
    char      module[64];   /* name of the module where the error occurred     */
    char      context[128]; /* name of the specific operation that failed      */
    time_t    timestamp;    /* unix timestamp recorded at the moment of failure */
} Error;

/* ── API ──────────────────────────────────────────────────────────────────── */

/*
 * error_init
 * ----------
 * Zero-initialises an Error record before use.
 * Must be called before passing an Error to any function, to guarantee
 * that all fields start from a known state rather than uninitialised memory.
 *
 * Parameters:
 *   err — pointer to the Error to initialise. No-op if NULL.
 *
 * Returns: nothing
 */
void error_init(Error *err);

/*
 * error_set
 * ---------
 * Populates an Error record with full failure details.
 * Called by any function that detects a failure, before returning to its caller.
 *
 * Parameters:
 *   err     — pointer to the Error to fill. No-op if NULL.
 *   code    — error code from the ErrorCode enum.
 *   module  — name of the module reporting the error (e.g. "queue").
 *             Falls back to "unknown" if NULL.
 *   context — name of the operation that failed (e.g. "enqueue").
 *             Falls back to "" if NULL.
 *   message — human-readable explanation. Falls back to "" if NULL.
 *
 * Returns: nothing
 */
void error_set(Error *err, ErrorCode code,
               const char *module, const char *context,
               const char *message);

/*
 * error_print
 * -----------
 * Prints the Error record to stderr in a JSON-like format.
 * Does nothing if err is NULL or err->code == ERR_NONE.
 *
 * Parameters:
 *   err — pointer to the Error to print.
 *
 * Returns: nothing
 */
void error_print(const Error *err);

/*
 * error_handle
 * ------------
 * Central error-handling entry point.
 * Currently delegates to error_print, but can be extended to write to a log
 * file, increment error counters, send alerts, etc., without touching callers.
 *
 * Parameters:
 *   err — pointer to the Error to handle. No-op if NULL or ERR_NONE.
 *
 * Returns: nothing
 */
void error_handle(const Error *err);

/*
 * error_code_str
 * --------------
 * Converts a numeric error code to its name as a string.
 * Useful for logging and diagnostics.
 *
 * Parameters:
 *   code — a value from the ErrorCode enum.
 *
 * Returns: pointer to a static string (do NOT free). e.g. "ERR_MUTEX_LOCK".
 *          Returns "ERR_UNKNOWN" for unrecognised codes.
 */
const char *error_code_str(ErrorCode code);

#endif /* ERROR_H */
