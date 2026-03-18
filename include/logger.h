//
//  logger.h
//  smart_home
//
//  Created by  Alexey on 12.03.2026.
//

#ifndef LOGGER_H
#define LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_LVL_ERROR = 0,
    LOG_LVL_WARN  = 1,
    LOG_LVL_INFO  = 2,
    LOG_LVL_DEBUG = 3
} log_level_t;

void logger_set_level(int level);
int  logger_get_level(void);
void logger_log(log_level_t level, const char *module, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define LOG_ERROR(mod, ...) logger_log(LOG_LVL_ERROR, mod, __VA_ARGS__)
#define LOG_WARN(mod, ...)  logger_log(LOG_LVL_WARN,  mod, __VA_ARGS__)
#define LOG_INFO(mod, ...)  logger_log(LOG_LVL_INFO,  mod, __VA_ARGS__)
#define LOG_DEBUG(mod, ...) logger_log(LOG_LVL_DEBUG, mod, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* LOGGER_H */

