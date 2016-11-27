
#include <ctype.h>
#include <string.h>
#include <time.h>

#include "utils.h"

// For logging.
#define _FILE "utils.c"

// Tokenizer delimiters.
#define TOKEN_DELIM " \t"

// Log paths.
#ifndef ANDROID_OS
int syslog_trace = 1;
#endif
int printf_trace = 0;

/*============================================================================
 * Public functions
 *============================================================================
 */

/**
 * @brief Split a command-line string into separate arguments.
 *
 * @param [in]  cmd_line  String to split.
 * @param [out] argv      Array to store the arguments.
 * @param [in]  argv_size Size of the argument array.
 * @þaram [out] argc      NUmber of arguments stored.
 *
 * @return Returns 0 at success, or -1 at failure.
 */
int split_cmd_line(const char *cmd_line, char *argv[], uint32_t argv_size,
                   uint32_t *argc)
{
    char *saveptr;
    uint32_t n, i;

    if (NULL == cmd_line) {
        ALOGE("%s:%d: Bad input param", _FILE, __LINE__);
        return -1;
    }

    if (0 == argv_size) {
        ALOGE("%s:%d: Size of argv is 0", _FILE, __LINE__);
        return -1;
    }

    // Split the command-line arguments.
    argv[0] = strtok_r((char *)cmd_line, TOKEN_DELIM, &saveptr);

    if (NULL == argv[0]) {
        ALOGE("%s:%d: Empty command-line", _FILE, __LINE__);
        return -1;
    }

    // First argument stored.
    n = 1;

    for (i = 1; i < argv_size; i++) {
        argv[i] = strtok_r(NULL, TOKEN_DELIM, &saveptr);
        if (NULL == argv[i]) {
            break;
        }
        n++;
    }

    *argc = n;

    return 0;
}

/**
 * @brief Get local calender time.
 *
 * @return Returns the claender time expressed in the local time zone.
 */
struct tm * get_time(void)
{
    time_t timer = time(NULL);
    return localtime(&timer);
}

/**
 * @brief Check if the string contains white-space only.
 *
 * @param [in] str String to check.
 */
int space_only(const char *str)
{
    while (isspace(*str)) {
        str++;
    }

    if ('\0' == *str) {
        return 1;
    }

    return 0;
}
