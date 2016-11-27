
#include <getopt.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "autoconf.h"
#include "mldproc.h"
#include "tracecmd.h"
#include "utils.h"

#define _FILE "tracecmd.c"

// MLD program name.
#define MLD_TOOL " mld "

// Dash marks the start of a command-line option.
#define OPTION_MARK " -"

// Max arguments on the command-line.
#define MAX_ARGC 64

// Trace commands.
enum tracecmd {
    TRACECMD_NONE,
    TRACECMD_START,
    TRACECMD_STOP,
    TRACECMD_QUERY,
    TRACECMD_CONFPATH
};

// Trace command option data.
struct traceopt {
    enum tracecmd cmd;
    char *startopt;
    char *stopopt;
};

// Thread synchronization.
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Short and long options for command-line parsing.
static const char *sopts = "s:k:qc";
static const struct option lopts[] = {
    {"start", required_argument, NULL, 's'},
    {"stop", required_argument, NULL, 'k'},
    {"query", no_argument, NULL, 'q'},
    {"confpath", no_argument, NULL, 'c'},
    {0, 0, 0, 0}
};

/*============================================================================
 * Public functions
 *============================================================================
 */

/**
 * @brief Parse trace command-line.
 *
 * @param [in]  cmd  Trace command.
 * @param [out] resp Response buffer.
 * @param [in]  len  Length of response buffer.
 *
 * @return Returns 0 at success, or -1 at failure.
 */
int tracecmd_exec(const char *cmd, char *resp, uint32_t len)
{
    char *trace_cmd;
    char *mld_cmd;
    char *argv[MAX_ARGC];
    uint32_t argc = 0;
    int opt;
    int rc = 0;
    struct traceopt trace;

    ALOGD("%s:%d: %s", _FILE, __LINE__, cmd);

    if (NULL == cmd) {
        ALOGE("%s:%d: Bad input param", _FILE, __LINE__);
        return -1;
    }

    // All trace commands take options.
    if (strstr(cmd, OPTION_MARK) == NULL) {
        ALOGE("%s:%d: Missing trace arguments", _FILE, __LINE__);
        return -1;
    }

    trace_cmd = strdup(cmd);

    if (NULL == trace_cmd) {
        ALOGE("%s:%d: Failed to allocate memory", _FILE, __LINE__);
        return -1;
    }

    // Check for MLD command-line.
    mld_cmd = strstr(trace_cmd, MLD_TOOL);

    if (mld_cmd) {
        // Terminate the trace command.
        *mld_cmd = '\0';

        // Set MLD command pointer to command start.
        mld_cmd++;
    }

    // Split the trace command-line.
    if (split_cmd_line(trace_cmd, argv, MAX_ARGC, &argc) == -1) {
        ALOGE("%s:%d: Failed to split command-line", _FILE, __LINE__);
        free(trace_cmd);
        return -1;
    }

    // Reset trace option data.
    trace.cmd = TRACECMD_NONE;
    trace.startopt = NULL;
    trace.stopopt = NULL;

    pthread_mutex_lock(&mutex);

    // Reset option parser (global state).
    optind = 0;

    // Parse command-line.
    while ((opt = getopt_long(argc, argv, sopts, lopts, NULL)) != -1) {
        switch (opt) {
        case 's':
            trace.cmd = TRACECMD_START;
            trace.startopt = optarg;
            break;

        case 'k':
            trace.cmd = TRACECMD_STOP;
            trace.stopopt = optarg;
            break;

        case 'q':
            trace.cmd = TRACECMD_QUERY;
            break;

        case 'c':
            trace.cmd = TRACECMD_CONFPATH;
            break;

        default:
            ALOGE("%s:%d: Option not recognized", _FILE, __LINE__);
            rc = -1;
            break;
        }

        // Parse only one option.
        break;
    }

    pthread_mutex_unlock(&mutex);

    // Execute command.
    switch (trace.cmd) {
    case TRACECMD_START:
        // Start MLD.
        if (mld_cmd) {
            rc = mldproc_start(trace.startopt, mld_cmd);
        } else {
            ALOGE("%s:%d: Missing MLD command-line", _FILE, __LINE__);
            rc = -1;
        }
        break;

    case TRACECMD_STOP:
        // Stop MLD.
        rc = mldproc_stop(trace.stopopt);
        break;

    case TRACECMD_QUERY:
        // Query MLD.
        rc = mldproc_query(resp, len);
        break;

    case TRACECMD_CONFPATH:
        // Get MLD configuration path.
        strncpy(resp, autoconf_getpath(), len);
        break;

    default:
        break;
    }

    free(trace_cmd);

    return rc;
}

