#ifndef BMX_TCPSER_DEBUG_H
#define BMX_TCPSER_DEBUG_H 1

#define LOG_TRACE 10
#define LOG_ALL 7
#define LOG_ENTER_EXIT 6
#define LOG_DEBUG 5
#define LOG_INFO 4
#define LOG_WARN 3
#define LOG_ERROR 2
#define LOG_FATAL 1
#define LOG_NONE 0

#ifdef BMC64_DEBUG_PROFILE
#include <stdio.h>
#define LOG(level, args...)                                                    \
    do {                                                                       \
        if ((level) <= LOG_INFO) {                                             \
            printf("tcpser: " args);                                           \
            printf("\r\n");                                                   \
        }                                                                      \
    } while (0)
#else
#define LOG(level, args...) do { } while (0)
#endif

#define LOG_ENTER() do { } while (0)
#define LOG_EXIT() do { } while (0)
#define ELOG(level, args...) LOG(level, args)

static inline int log_init(void) { return 0; }
static inline void log_set_level(int a) { (void)a; }
static inline int log_get_trace_flags(void) { return 0; }
static inline void log_set_trace_flags(int a) { (void)a; }
static inline void log_trace(int type, unsigned char *line, int len)
{
    (void)type;
    (void)line;
    (void)len;
}

#endif
