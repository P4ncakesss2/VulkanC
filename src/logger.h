#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

#define LOG_COLOR_GREEN  "\033[0;32m"
#define LOG_COLOR_YELLOW "\033[0;33m"
#define LOG_COLOR_RED    "\033[0;31m"
#define LOG_COLOR_RESET  "\033[0m"

#define LOG_INFO(fmt, ...) \
    printf(LOG_COLOR_GREEN "[INFO] " LOG_COLOR_RESET fmt "\n", ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    fprintf(stderr, LOG_COLOR_YELLOW "[WARN] (%s:%d): " LOG_COLOR_RESET fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    fprintf(stderr, LOG_COLOR_RED "[ERROR] (%s:%d): " LOG_COLOR_RESET fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#endif