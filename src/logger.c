#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

/* Global logger instance used by all modules via the LOG_* macros */
Logger g_logger;

/* Returns a fixed-width label string for a given log level */
static const char *level_to_str(LogLevel level)
{
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO ";
        case LOG_WARN:  return "WARN ";
        case LOG_ERROR: return "ERROR";
        default:        return "?????";
    }
}

int logger_init(Logger *logger, LogLevel min_level, FILE *output)
{
    if (!logger) return -1;

    /* Zero the struct first to avoid any stale field values */
    memset(logger, 0, sizeof(*logger));
    logger->min_level   = min_level;
    logger->output      = output ? output : stderr; /* default to stderr if no output provided */

    /* Create the mutex that serialises concurrent log writes */
    int rc = pthread_mutex_init(&logger->mutex, NULL);
    if (rc != 0) return rc;

    /* Mark as ready only after everything succeeds */
    logger->initialized = 1;
    return 0;
}

void logger_destroy(Logger *logger)
{
    if (!logger || !logger->initialized) return;
    pthread_mutex_destroy(&logger->mutex);
    logger->initialized = 0; /* prevent any further use after destruction */
}

void logger_log(Logger *logger, LogLevel level,
                const char *module, const char *fmt, ...)
{
    if (!logger || !logger->initialized) return;

    /* Early exit before acquiring the mutex — avoids lock contention for
       filtered-out messages, which happen frequently at INFO level */
    if (level < logger->min_level) return;

    /* Format the current time for the log prefix */
    char   time_buf[32];
    time_t now     = time(NULL);
    struct tm *tmi = localtime(&now);
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tmi);

    /* Lock so that the entire line is written atomically.
       Without this, two threads could interleave their characters. */
    pthread_mutex_lock(&logger->mutex);

    fprintf(logger->output, "[%s] [%s] [%-12s] ",
            time_buf, level_to_str(level), module ? module : "unknown");

    /* va_list lets us forward the variadic arguments to vfprintf */
    va_list args;
    va_start(args, fmt);
    vfprintf(logger->output, fmt, args);
    va_end(args);

    fputc('\n', logger->output);

    /* Flush immediately so logs appear in order, even if the process crashes */
    fflush(logger->output);

    pthread_mutex_unlock(&logger->mutex);
}

void logger_set_level(Logger *logger, LogLevel level)
{
    if (logger) logger->min_level = level;
}
