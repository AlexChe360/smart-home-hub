//
//  logger.c
//  smart_home
//
//  Created by  Alexey on 12.03.2026.
//

#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static int g_log_level = LOG_LVL_INFO;

void logger_set_level(int level)
{
    g_log_level = level;
}

int logger_get_level(void)
{
    return g_log_level;
}

void logger_log(log_level_t level, const char *module, const char *fmt, ...)
{
    if ((int)level > g_log_level)
        return;

    static const char *level_str[] = {"ERROR", "WARN", "INFO", "DEBUG"};

    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &tm_buf);

    fprintf(stderr, "%s [%s] [%s] ", timebuf, level_str[(int)level], module);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
}
