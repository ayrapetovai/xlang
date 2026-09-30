#include "logger.h"

#include <stdio.h>
#include <time.h>

static LogLevel minimum_level = LOG_LEVEL_INFO;

static const char *level_name(LogLevel level)
{
    switch (level) {
    case LOG_LEVEL_DEBUG: return "DEBUG";
    case LOG_LEVEL_INFO:  return "INFO";
    case LOG_LEVEL_WARN:  return "WARN";
    case LOG_LEVEL_ERROR: return "ERROR";
    case LOG_LEVEL_FATAL: return "FATAL";
    default:              return "UNKNOWN";
    }
}

void logger_set_level(LogLevel level)
{
    minimum_level = level;
}

void logger_log(
    LogLevel level,
    const char *file,
    int line,
    const char *function,
    const char *format,
    ...)
{
    if (level < minimum_level) {
        return;
    }

    time_t now = time(NULL);
    struct tm tm_now;

    if (localtime_r(&now, &tm_now) == NULL) {
        return;
    }

    char timestamp[32];

    if (strftime(
            timestamp,
            sizeof timestamp,
            "%Y-%m-%dT%H:%M:%S%z",
            &tm_now) == 0) {
        return;
    }

    FILE *output = level >= LOG_LEVEL_ERROR ? stderr : stdout;

    fprintf(
        output,
        "%s %-5s %s:%d %s(): ",
        timestamp,
        level_name(level),
        file,
        line,
        function
    );

    va_list arguments;
    va_start(arguments, format);
    vfprintf(output, format, arguments);
    va_end(arguments);

    fputc('\n', output);
    fflush(output);
}

