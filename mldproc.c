
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "cmdserver.h"
#include "mldproc.h"
#include "utils.h"

// For logging.
#define _FILE "mldproc.c"

// Max arguments on the command-line.
#define MAX_ARGC 64

// The MLD binary.
#define MLD_BIN "/system/bin/mld"

// MLD options.
#define MLD_OPT_DONT_DEMONIZE "-d"

// Path delimiter.
#define PATH_DELIM '/'

// Modem CPU.
#define MACC "LOG_D_ACC"
#define MAPP "LOG_D_APP"

// Permisson when creating directories.
#define DIR_PERM 0777

struct session {
    struct session *next;
    pid_t pid;
    char *name;
};

// Session list head and tail.
static struct session *head = NULL;
static struct session *tail = NULL;

// Forward declarations.
static int add_session(pid_t pid, const char *name);
static struct session * get_session(const char *name, struct session **prev);
static int remove_session(const char *name);
static int session_active(const char *name);
static int add_mld_option(const char *option, char *argv[], uint32_t *argc);
static int mkpath(const char *path, mode_t mode);

/*============================================================================
 * Public functions
 *============================================================================
 */

/**
 * @brief Start a MLD log session.
 *
 * @param [in] name Unique session name.
 * @param [in] cmd  MLD command-line (without log file name).
 *
 * @return Returns 0 at success, or -1 at failure.
 */
int mldproc_start(const char *name, const char *cmd)
{
    struct tm *time;
    char *mcpu = "";
    char mld_cmd[CMD_LINE_LENGTH];
    char *argv[MAX_ARGC + 1]; // + 1 for null pointer termination.
    uint32_t argc;
    pid_t pid;

    if (NULL == name || NULL == cmd) {
        ALOGE("%s:%d: Bad input", _FILE, __LINE__);
        return -1;
    }

    // Make sure the session name doesn't already exist.
    if (session_active(name)) {
        ALOGE("%s:%d: Session name already exist (name: %s)", _FILE, __LINE__,
              name);
        return -1;
    }

    time = get_time();

    if (strstr(cmd, MACC)) {
        mcpu = "acc";
    } else if (strstr(cmd, MAPP)) {
        mcpu = "app";
    }

    // Complete the command-line with a log file name.
    if (time) {
        snprintf(mld_cmd, CMD_LINE_LENGTH,
                 "%s/%04d-%02d-%02d_%02dh%02dm%02ds_%s.log", cmd,
                 time->tm_year + 1900, time->tm_mon + 1, time->tm_mday,
                 time->tm_hour, time->tm_min, time->tm_sec, mcpu);
    } else {
        snprintf(mld_cmd, CMD_LINE_LENGTH, "%s/log_%s.log", cmd, mcpu);
    }

    // Split the MLD command-line.
    if (split_cmd_line(mld_cmd, argv, MAX_ARGC, &argc) == -1) {
        ALOGE("%s:%d: Missing MLD arguments", _FILE, __LINE__);
        return -1;
    }

    // Create the log path.
    if (mkpath(argv[argc - 1], DIR_PERM) == -1) {
        ALOGE("%s:%d: Failed to create MLD log path", _FILE, __LINE__);
        return -1;
    }

    // Make sure MLD doesn't start as a demon.
    if (add_mld_option(MLD_OPT_DONT_DEMONIZE, argv, &argc) == -1) {
        ALOGE("%s:%d: Failed to add mandatory MLD option", _FILE, __LINE__);
        return -1;
    }

    // Finalize the option vector for execve() call.
    argv[0] = MLD_BIN;
    argv[argc] = NULL;

    // Create a new process for MLD.
    if ((pid = fork()) == -1) {
        ALOGE("%s:%d: Failed to create process for MLD", _FILE, __LINE__);
        return -1;
    }

    if (0 == pid) {
        // Close inherited open file descriptor in the new child process.
        cmdserver_closefd();
        
        // Execute the MLD binary with the provided arguments.
        if (execve(MLD_BIN, argv, NULL) == -1) {
            ALOGE("%s:%d: Failed to execute MLD", _FILE, __LINE__);
        }

        // In case execve() fails.
        _exit(1);
    } else {
        // Store MLD session.
        if (add_session(pid, name) == -1) {
            ALOGE("%s:%d: Failed store session (name: %s)", _FILE, __LINE__,
                  name);
            return -1;
        }
    }

    return 0;
}

/**
 * @brief Stop a MLD log session.
 *
 * @param [in] name Unique session name.
 *
 * @return Returns 0 at success, or -1 at failure.
 */
int mldproc_stop(const char *name)
{
    struct session *mld, *tmp;

    if (NULL == name) {
        ALOGE("%s:%d: Bad input", _FILE, __LINE__);
        return -1;
    }

    mld = get_session(name, &tmp);

    if (mld) {
        if (kill(mld->pid, SIGTERM) == -1) {
            ALOGE("%s:%d: Failed to send termination signal (name: %s, pid: %d)",
                  _FILE, __LINE__, name, mld->pid);
        }

        if (remove_session(name) == -1) {
            ALOGE("%s:%d: Failed to remove session (name: %s)", _FILE,
                  __LINE__, name);
            return -1;
        }
    } else {
        ALOGE("%s:%d: Session not active (name: %s)", _FILE, __LINE__, name);
        return -1;
    }

    return 0;
}

/**
 * @brief Query for a MLD log session. The response buffer will be populated
 *        by active session names sperated by space.
 *
 * @param [out] resp Response buffer.
 * @param [in]  len  Length of response buffer.
 *
 * @return Returns 0 at success, or -1 at failure.
 */
int mldproc_query(char *resp, uint32_t len)
{
    struct session *p;
    uint32_t n = 0;

    if (NULL == resp) {
        ALOGE("%s:%d: Bad input", _FILE, __LINE__);
        return -1;
    }

    p = head;

    while (p) {
        size_t namelen = strlen(p->name) + 1; // + 1 for space separator.
        if ((namelen + n) >= len) {
            ALOGE("%s:%d: Not enough space in the response buffer", _FILE,
                  __LINE__);
            return -1;
        }

        // Separate session names with space.
        if (n > 0) {
            strcat(resp, " ");
        }

        // Add session name and update position.
        strcat(resp, p->name);
        n += namelen;

        p = p->next;
    }

    return 0;
}

/*============================================================================
 * Private functions
 *============================================================================
 */

/**
 * @brief Add a session to the list of active sessions.
 *
 * @param [in] pid  Process ID of the MLD session.
 * @param [in] name Unique name of the MLD session.
 *
 * @return Returns 0 at success, or -1 at failure.
 */
static int add_session(pid_t pid, const char *name)
{
    struct session *node;

    node = malloc(sizeof(struct session));

    if (node) {
        node->next = NULL;
        node->pid = pid;
        node->name = malloc(strlen(name) + 1);

        if (node->name) {
            strcpy(node->name, name);

            if (NULL == head) {
                // First session.
                head = node;
            } else {
                // Add session last in list.
                tail->next = node;
            }
            tail = node;
        } else {
            ALOGE("%s:%d: Failed to allocate memory", _FILE, __LINE__);
            free(node);
            return -1;
        }
    } else {
        ALOGE("%s:%d: Failed to allocate memory", _FILE, __LINE__);
        return -1;
    }

    ALOGD("%s:%d: Added log session (name: %s, pid: %d)", _FILE, __LINE__,
          node->name, node->pid);

    return 0;
}

/**
 * @brief Get named session.
 *
 * @param [in]  name Unique session name.
 * @param [out] prev Pointer to the session that precedes the returned one.
 *
 * @return Returns a valid pointer to the session if found, else NULL.
 */
static struct session * get_session(const char *name, struct session **prev)
{
    struct session *ptr = head;
    struct session *tmp = NULL;

    while (ptr) {
        if (strcmp(ptr->name, name) == 0) {
            break;
        }
        tmp = ptr;
        ptr = ptr->next;
    }

    *prev = tmp;

    return ptr;
}

/**
 * @brief Remove named session from the list.
 *
 * @param [in] name Unique session name.
 *
 * @return Returns 0 at success, or -1 at failure.
 */
static int remove_session(const char *name)
{
    struct session *prev = NULL;
    struct session *curr = NULL;

    curr = get_session(name, &prev);

    if (NULL == curr) {
        ALOGE("%s:%d: Session not found", _FILE, __LINE__);
        return -1;
    }

    if (prev) {
        prev->next = curr->next;
    } else {
        // The session to be removed (curr) is head.
        head = curr->next;
    }

    if (curr == tail) {
        tail = prev;
    }

    // Delete session data.
    ALOGD("%s:%d: Removed log session (name: %s)", _FILE, __LINE__, curr->name);
    free(curr->name);
    free(curr);

    return 0;
}

/**
 * @brief Check if the session is active.
 *
 * @param [in] name Session name.
 *
 * @return Returns 1 if it is active, else 0.
 */
static int session_active(const char *name)
{
    struct session *p;

    if (get_session(name, &p)) {
        return 1;
    }

    return 0;
}

/**
 * @brief Add an option to the MLD command-line.
 *
 * @param [in]     option Option to be added.
 * @param [in out] argv   Current MLD options.
 * @param [in out] argc   Current number of MLD options.
 *
 * @return Returns 0 at success, or -1 at failure.
 */
static int add_mld_option(const char *option, char *argv[], uint32_t *argc)
{
    uint32_t i, n = *argc;

    // Check if the option is already in place.
    for (i = 0; i < n; i++) {
        if (strcmp(argv[i], option) == 0) {
            return 0;
        }
    }

    // Make sure there are room for one extra option.
    if (*argc >= MAX_ARGC) {
        ALOGE("%s:%d: Not enough space in arg vector", _FILE, __LINE__);
        return -1;
    }

    // Add option to the arg vector.
    for (i = n; i > 1; i--) {
        argv[i] = argv[i - 1];
    }

    argv[1] = (char *)option;
    *argc = n + 1;

    return 0;
}

/**
 * @brief Create directory path.
 *
 * @param [in] path Directory path to create.
 * @param [in] mode Permission to use when creating directories.
 *
 * @return Returns 0 at success, or -1 at failure.
 */
static int mkpath(const char *path, mode_t mode)
{
    const char *pos = path;
    uint32_t len;
    char tmp[MAX_PATH_LEN];
    struct stat sb;

    if (NULL == path) {
        ALOGE("%s:%d: Bad input", _FILE, __LINE__);
        return -1;
    }

    // Check for root.
    if (strlen(path) == 1 && PATH_DELIM == *path) {
        return 0;
    }

    // Check each component of the path.
    while ((pos = strchr(pos, PATH_DELIM)) != NULL) {
        if (pos == path) {
            // Path starts with PATH_DELIM.
            pos++;
            continue;
        }

        // Intermediate path length.
        len = pos - path;

        if (len >= MAX_PATH_LEN) {
            ALOGE("%s:%d: Long path", _FILE, __LINE__);
            return -1;
        }

        strncpy(tmp, path, len);
        tmp[len] = '\0';

        // Create component if it doesn't exist.
        if ((stat(tmp, &sb) == -1) && (ENOENT == errno)) {
            if (mkdir(tmp, mode) == -1) {
                ALOGE("%s:%d: Failed to create directory (errno=%d)", _FILE,
                      __LINE__, errno);
                return -1;
            }
        }

        pos++;
    }

    // If path doesn't end with PATH_DELIM we need to create one more
    // directory.
    if (PATH_DELIM != path[strlen(path) - 1]) {
        if ((stat(path, &sb) == -1) && (ENOENT == errno)) {
            if (mkdir(path, mode) == -1) {
                ALOGE("%s:%d: Failed to create tail directory (errno=%d)",
                      _FILE, __LINE__, errno);
                return -1;
            }
        }
    }

    return 0;
}
