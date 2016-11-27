
#include <getopt.h>
#include <signal.h>
#include <stdlib.h>

#include "autoconf.h"
#include "cmdserver.h"
#include "utils.h"

#define _FILE "main.c"

// Short and long options for command-line parsing.
static const char *shortopts = "p:c:";
static const struct option longopts[] = {
    {"port", required_argument, NULL, 'p'},
    {"confpath", required_argument, NULL, 'c'},
    {0, 0, 0, 0}
};

/*============================================================================
 * Public functions
 *============================================================================
 */

/**
 * @brief Program entry point.
 */
int main(int argc, char *argv[])
{
    int opt;
    const char *port = NULL;
    const char *confpath = NULL;

    // Prevent creation of child zombie processes.
    signal(SIGCHLD, SIG_IGN);

    // Parse command-line.
    while ((opt = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
        switch (opt) {
        case 'p':
            port = optarg;
            break;

        case 'c':
            confpath = optarg;
            break;
        }
    }

    // Check config files for autostart option.
    autoconf_init(confpath);

    // Start the command server.
    if (cmdserver_start(port) == -1) {
        ALOGE("%s:%d: Failed to start command server", _FILE, __LINE__);
        return -1;
    }

    // Wait while the server is running.
    cmdserver_wait();

    return 0;
}
