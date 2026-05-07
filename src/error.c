#include "error.h"
#include <stdio.h>
#include <string.h>

void error_init(Error *err)
{
    if (!err) return;
    /* Zero every byte so no field contains uninitialised garbage */
    memset(err, 0, sizeof(*err));
}

void error_set(Error *err, ErrorCode code,
               const char *module, const char *context,
               const char *message)
{
    if (!err) return;

    err->code      = code;
    err->timestamp = time(NULL); /* capture the exact moment of failure */

    /* Use ternary guards so callers can safely pass NULL for any string field */
    strncpy(err->module,  module  ? module  : "unknown", sizeof(err->module)  - 1);
    strncpy(err->context, context ? context : "",        sizeof(err->context) - 1);
    strncpy(err->message, message ? message : "",        sizeof(err->message) - 1);

    /* strncpy does not null-terminate when the source is longer than the limit,
       so we force the final byte to '\0' to prevent out-of-bounds reads */
    err->module[sizeof(err->module)   - 1] = '\0';
    err->context[sizeof(err->context) - 1] = '\0';
    err->message[sizeof(err->message) - 1] = '\0';
}

void error_print(const Error *err)
{
    if (!err || err->code == ERR_NONE) return;

    /* Convert the unix timestamp to a human-readable string */
    char   time_buf[32];
    struct tm *tm_info = localtime(&err->timestamp);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    /* Print to stderr so error output stays separate from normal stdout */
    fprintf(stderr,
        "{\n"
        "  \"error_code\": %d,\n"
        "  \"error_name\": \"%s\",\n"
        "  \"message\":    \"%s\",\n"
        "  \"module\":     \"%s\",\n"
        "  \"context\":    \"%s\",\n"
        "  \"timestamp\":  \"%s\"\n"
        "}\n",
        err->code,
        error_code_str(err->code), /* add the symbolic name alongside the number */
        err->message,
        err->module,
        err->context,
        time_buf);
}

void error_handle(const Error *err)
{
    if (!err || err->code == ERR_NONE) return;
    /* Single call site for all error handling — extend here without
       touching any of the callers spread across other modules */
    error_print(err);
}

const char *error_code_str(ErrorCode code)
{
    /* Map every enum value to its name for readable log output */
    switch (code) {
        case ERR_NONE:           return "ERR_NONE";
        case ERR_MUTEX_INIT:     return "ERR_MUTEX_INIT";
        case ERR_MUTEX_LOCK:     return "ERR_MUTEX_LOCK";
        case ERR_MUTEX_UNLOCK:   return "ERR_MUTEX_UNLOCK";
        case ERR_COND_INIT:      return "ERR_COND_INIT";
        case ERR_COND_WAIT:      return "ERR_COND_WAIT";
        case ERR_COND_SIGNAL:    return "ERR_COND_SIGNAL";
        case ERR_COND_BROADCAST: return "ERR_COND_BROADCAST";
        case ERR_MEMORY_ALLOC:   return "ERR_MEMORY_ALLOC";
        case ERR_QUEUE_FULL:     return "ERR_QUEUE_FULL";
        case ERR_QUEUE_EMPTY:    return "ERR_QUEUE_EMPTY";
        case ERR_QUEUE_SHUTDOWN: return "ERR_QUEUE_SHUTDOWN";
        case ERR_THREAD_CREATE:  return "ERR_THREAD_CREATE";
        case ERR_THREAD_JOIN:    return "ERR_THREAD_JOIN";
        case ERR_POOL_SHUTDOWN:  return "ERR_POOL_SHUTDOWN";
        case ERR_NULL_POINTER:   return "ERR_NULL_POINTER";
        case ERR_INVALID_ARG:    return "ERR_INVALID_ARG";
        default:                 return "ERR_UNKNOWN";
    }
}
