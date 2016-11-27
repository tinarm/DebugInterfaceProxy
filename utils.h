
#ifndef UTILS_H
#define UTILS_H

#define BINNAME "DIP"

extern int printf_trace;

#ifdef ANDROID_OS
    #define LOG_TAG BINNAME
    #include <utils/Log.h>
    #define OPENLOG(facility) ((void)0)
    #define EXTRADEBUG(fmt , __args__...) \
        do { \
            if (printf_trace) \
                ALOGD(fmt, ##__args__); \
        } while (0)
#else
    #include <syslog.h>
    extern int syslog_trace;
    #define SYSLOG(prio, fmt , __args__...) \
    do { \
        if (syslog_trace) \
            syslog(prio, "%s: " fmt "\r\n", __func__, ##__args__); \
        if (printf_trace) \
            printf("%s: " fmt "\r\n", __func__, ##__args__); \
    } while (0)

    #define OPENLOG(facility) openlog(BINNAME, LOG_PID | LOG_CONS, facility)
    #define LOG(priority, format, ...) SYSLOG(priority, format, ##__VA_ARGS__)
    #define LOGV(format, ...)   LOG(LOG_INFO, format, ##__VA_ARGS__)
    #define LOGD(format, ...)   LOG(LOG_DEBUG, format, ##__VA_ARGS__)
    #define LOGI(format, ...)   LOG(LOG_INFO, format, ##__VA_ARGS__)
    #define LOGW(format, ...)   LOG(LOG_WARNING, format, ##__VA_ARGS__)
    #define LOGE(format, ...)   LOG(LOG_ERR, format, ##__VA_ARGS__)
    #define EXTRADEBUG(format, ...)  LOG(LOG_DEBUG, format, ##__VA_ARGS__)
#endif

#define MAX_PATH_LEN    128
#define MAX_NAME_LEN    128
#define CMD_LINE_LENGTH 256

#define UNUSED(a) ((void)(a))


int split_cmd_line(const char *cmd_line, char *argv[], uint32_t argv_size,
                   uint32_t *argc);
struct tm * get_time(void);
int space_only(const char *str);

#endif // UTILS_H

