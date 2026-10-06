#ifndef LOGGER_H
#define LOGGER_H

#include <stdarg.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL,
    LOG_LEVEL_OFF,
} LogLevel;

void logger_set_level(LogLevel level);
void logger_log(
    LogLevel level,
    const char *file,
    int line,
    const char *function,
    const char *format,
    ...
);

#define LOG_DEBUG(...) \
    logger_log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define LOG_INFO(...) \
    logger_log(LOG_LEVEL_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define LOG_WARN(...) \
    logger_log(LOG_LEVEL_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define LOG_ERROR(...) \
    logger_log(LOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define LOG_FATAL(...) \
    logger_log(LOG_LEVEL_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)

#endif

