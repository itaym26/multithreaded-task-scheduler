#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <pthread.h>

/* ── Log levels ───────────────────────────────────────────────────────────────
 * Severity levels in ascending order.
 * The logger only prints messages whose level >= the configured min_level.
 *   DEBUG — very detailed tracing, for development only
 *   INFO  — normal operational events (thread started, task completed)
 *   WARN  — something unexpected but not fatal
 *   ERROR — serious failure that needs attention
 * ────────────────────────────────────────────────────────────────────────── */
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3,
} LogLevel;

/* ── Logger state ─────────────────────────────────────────────────────────────
 * Holds all runtime state for one logger instance.
 * The mutex serialises output writes so that log lines from different threads
 * never interleave with each other.
 * ────────────────────────────────────────────────────────────────────────── */
typedef struct {
    LogLevel        min_level;   /* messages below this level are silenced    */
    FILE           *output;      /* write destination: stdout, stderr, or file */
    pthread_mutex_t mutex;       /* prevents concurrent writes from garbling output */
    int             initialized; /* 1 = ready to use, 0 = not yet initialised  */
} Logger;

/* Global logger instance shared by all modules — defined in logger.c */
extern Logger g_logger;

/* ── API ──────────────────────────────────────────────────────────────────── */

/*
 * logger_init
 * -----------
 * Initialises the logger and creates its internal mutex.
 * Must be called once before any logging macros or logger_log calls.
 *
 * Parameters:
 *   logger    — pointer to the Logger struct to initialise.
 *   min_level — minimum severity to print (LOG_DEBUG / LOG_INFO / ...).
 *   output    — write destination. Falls back to stderr if NULL.
 *
 * Returns: 0 on success, a non-zero pthreads error code on failure.
 */
int logger_init(Logger *logger, LogLevel min_level, FILE *output);

/*
 * logger_destroy
 * --------------
 * Destroys the logger's mutex and marks it as uninitialised.
 * Must be called at program exit to release OS resources.
 *
 * Parameters:
 *   logger — pointer to the Logger to destroy.
 *
 * Returns: nothing
 */
void logger_destroy(Logger *logger);

/*
 * logger_log
 * ----------
 * Writes a timestamped log line in a thread-safe manner.
 * The line includes: time, level, module name, and a printf-style message.
 * Returns immediately without locking if level < min_level.
 *
 * Parameters:
 *   logger — pointer to the Logger (usually &g_logger).
 *   level  — severity of this message.
 *   module — name of the calling module (e.g. "queue").
 *   fmt    — printf-style format string.
 *   ...    — variadic arguments matching the format string.
 *
 * Returns: nothing
 */
void logger_log(Logger *logger, LogLevel level,
                const char *module, const char *fmt, ...);

/*
 * logger_set_level
 * ----------------
 * Changes the minimum log level at runtime without restarting the program.
 * Useful for switching between verbose (DEBUG) and quiet (INFO/WARN) modes.
 *
 * Parameters:
 *   logger — pointer to the Logger to update.
 *   level  — the new minimum level.
 *
 * Returns: nothing
 */
void logger_set_level(Logger *logger, LogLevel level);

/* ── Convenience macros ───────────────────────────────────────────────────────
 * Short forms that target the global g_logger.
 * Instead of: logger_log(&g_logger, LOG_INFO, "queue", "size=%d", n);
 * Just write:  LOG_INFO("queue", "size=%d", n);
 * ────────────────────────────────────────────────────────────────────────── */
#define LOG_DEBUG(mod, ...) logger_log(&g_logger, LOG_DEBUG, (mod), __VA_ARGS__)
#define LOG_INFO(mod, ...)  logger_log(&g_logger, LOG_INFO,  (mod), __VA_ARGS__)
#define LOG_WARN(mod, ...)  logger_log(&g_logger, LOG_WARN,  (mod), __VA_ARGS__)
#define LOG_ERROR(mod, ...) logger_log(&g_logger, LOG_ERROR, (mod), __VA_ARGS__)

#endif /* LOGGER_H */
