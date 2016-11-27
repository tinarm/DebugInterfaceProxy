
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "autoconf.h"
#include "mldproc.h"
#include "utils.h"

// For logging.
#define _FILE "autoconf.c"

// Default location of config files.
#define AUTOCONF_PATH "/sdcard/mld.conf"

// Config file suffix.
#define AUTOCONF_SUFFIX ".conf"

// Number of autostart command-line args.
#define AUTOSTART_ARGS 2

// Autostart command.
#define AUTOSTART_CMD "AUTOSTART"
#define AUTOSTART_YES "1"

// Path to look for configuration files.
static char confpath[MAX_PATH_LEN] = AUTOCONF_PATH;

// Forward declarations.
static void parse_conf(const char *file);


/*============================================================================
 * Public functions
 *============================================================================
 */

/**
 * @brief Check all MLD configuration files for the autostart flag.
 *
 * @param [in] path Location of configuration files.
 */
void autoconf_init(const char *path)
{
    struct dirent *file;
    DIR *dir;

    if (NULL != path) {
        strncpy(confpath, path, MAX_PATH_LEN);
        confpath[MAX_PATH_LEN - 1] = '\0';
    } 

    dir = opendir(confpath);

    if (NULL == dir) {
        ALOGD("%s:%d: MLD configuration path does not exist", _FILE,
              __LINE__);
        return;
    }

    while ((file = readdir(dir))) {
        parse_conf(file->d_name);
    }

    closedir(dir);
    return;
}

/**
 * @brief Get configuration path.
 *
 * @return Pointer to the path string.
 */
char * autoconf_getpath(void)
{
    return confpath;
}

/*============================================================================
 * Private functions
 *============================================================================
 */

/**
 * @brief Parse input file for MLD configuration.
 *
 * @param [in] filename Name of file to parse.
 */
static void parse_conf(const char *filename)
{
    char *suffix, *session;
    FILE  *file;
    char buf[CMD_LINE_LENGTH];
    char conf[CMD_LINE_LENGTH];
    char *argv[AUTOSTART_ARGS];
    uint32_t argc;
    uint32_t start = 0;

    snprintf(conf, CMD_LINE_LENGTH, "%s/%s", AUTOCONF_PATH, filename);

    // Check if the suffix is correct.
    suffix = strrchr(conf, '.');

    if (NULL == suffix || (strcmp(suffix, AUTOCONF_SUFFIX) != 0)) {
        return;
    }

    file = fopen(conf, "r");

    if (NULL == file) {
        ALOGD("%s:%d: Failed to open config file %s (errno=%d)", _FILE,
              __LINE__, filename, errno);
        return;
    }

    // Set MLD session name by removing the suffix from the filename.
    session = strrchr(conf, '/') + 1;
    *suffix = '\0';

    if (NULL == session) {
        ALOGD("%s:%d: Failed to get session name", _FILE, __LINE__);
        return;
    }

    while (fgets(buf, CMD_LINE_LENGTH, file)) {
        if (0 == start) {
            // Look for autostart command.
            if (split_cmd_line(buf, argv, AUTOSTART_ARGS, &argc) != -1) {
                if (AUTOSTART_ARGS == argc &&
                        strncmp(argv[0], AUTOSTART_CMD, strlen(AUTOSTART_CMD)) == 0 &&
                        strncmp(argv[1], AUTOSTART_YES, strlen(AUTOSTART_YES)) == 0) {
                    start = 1;
                }
            }
        } else {
            // Look for MLD command-line.
            if (!space_only(buf)) {
                // Remove trailing newline or EOF.
                size_t len = strlen(buf);
                buf[len - 1] = '\0';

                // Start a new MLD log session.
                (void)mldproc_start(session, buf);
            }
        }
    }

    fclose(file);
}
