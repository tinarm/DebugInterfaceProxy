
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "cmdserver.h"
#include "tracecmd.h"
#include "utils.h"

#define _FILE "cmdserver.c"

// Default TCP port.
#define DEFAULT_PORT "3002"

// Queue size for pending server connections.
#define BACKLOG 2

// Max number of connected clients.
#define MAX_CONNECTED_CLIENTS 3

// Client acknowledgments.
#define RES_OK "OK\n"
#define RES_KO "KO\n"

// Empty string.
#define NULL_STR ""

// Line ending.
#define LINE_END "\n"
#define ASCII_LF '\n'

enum status {
    STOPPED,
    RUNNING
};

struct server_data {
    pthread_t thread;
    int sockfd;
    enum status status;
};

struct client_data {
    uint32_t ref_count;
};

// Thread synchronization.
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Server data.
static struct server_data server;

// Client data.
static struct client_data client;

// Process ID.
static pid_t pid;

// Forward declarations.
static void * server_thread(void *arg);
static void * client_thread(void *arg);
static enum status get_server_status(void);
static void set_server_status(enum status status);
static int accept_connection(void);
static void inc_ref_count(void);
static void dec_ref_count(void);
static int dispatch_command(const char *cmd, char *resp, uint32_t len);
static int recv_line(int fd, char *line, uint32_t size);
static int send_buf(int fd, const char *buf, uint32_t size);
static int send_response(int fd, int status, char *resp, uint32_t size);

/*============================================================================
 * Public functions
 *============================================================================
 */

/**
 * @brief Start the comand server.
 *
 * @param [in] port TCP port for the service to listen on.
 *
 * @return Returns 0 at success and -1 at failure.
 */
int cmdserver_start(const char *port)
{
    int rc;
    const char *tcp_port;
    struct addrinfo *servinfo, *info;
    struct addrinfo hints;

    // Save the process ID.
    pid = getpid();

    // Make sure it's not already running.
    if (get_server_status() == RUNNING) {
        return -1;
    }

    // Server not started yet.
    server.status = STOPPED;
    server.sockfd = -1;

    // Init client connection data.
    client.ref_count = 0U;

    // Setup address structure(s).
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // Handle both IPv4 and IPv6.
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    // Check server port.
    tcp_port = (NULL == port) ? DEFAULT_PORT : port;

    if ((rc = getaddrinfo(NULL, tcp_port, &hints, &servinfo)) != 0) {
        ALOGE("%s:%d: Failed to get address info (%s)", _FILE, __LINE__,
             gai_strerror(rc));
        return -1;
    }

    // Bind to the first located socket.
    for (info = servinfo; info != NULL; info = info->ai_next) {
        if ((server.sockfd = socket(info->ai_family, info->ai_socktype,
                                    info->ai_protocol)) == -1) {
            continue;
        }

        if (bind(server.sockfd, info->ai_addr, info->ai_addrlen) == -1) {
            close(server.sockfd);
            continue;
        }

        // At this point we are successfully bound to a socket.
        break;
    }

    if (NULL == info) {
        ALOGE("%s:%d: Failed to bind to socket", _FILE,
              __LINE__);
        freeaddrinfo(servinfo);
        return -1;
    }

    freeaddrinfo(servinfo);

    if (listen(server.sockfd, BACKLOG) == -1) {
        ALOGE("%s:%d: Refused to listen to server socket", _FILE,
              __LINE__);
        return -1;
    }

    // Start server thread.
    if (pthread_create(&server.thread, NULL, server_thread, NULL) != 0) {
        ALOGE("%s:%d: Failed to create server thread", _FILE,
              __LINE__);
        return -1;
    }

    return 0;
}

/**
 * @brief Wait while the server is running.
 */
void cmdserver_wait(void)
{
    (void)pthread_join(server.thread, NULL);
}

/**
 * @brief Close the server socker.
 *
 * NOTE! This is only intended for child processes created with fork().
 */
void cmdserver_closefd(void)
{
    ALOGD("%s:%d: pid=%d, getpid()=%d", _FILE, __LINE__, pid, getpid());
    if (getpid() == pid) {
        return;
    }

    if (server.sockfd != -1) {
        close(server.sockfd);
    }
}

/*============================================================================
 * Private functions
 *============================================================================
 */

/**
 * @brief Waiting for clients to connect.
 *
 * @param [in] arg <Not in use>.
 *
 * @return Returns NULL at thread exit.
 */
static void * server_thread(void *arg)
{
    struct sockaddr_storage caddr; // Client address info.
    socklen_t caddr_len = sizeof(caddr);
    int *fd;
    pthread_t conn;

    UNUSED(arg);

    set_server_status(RUNNING);

    while (1) {
        // Allocate file descriptor for the client connection socket. The
        // client is responsible to free this memory.
        fd = malloc(sizeof(*fd));

        if (NULL == fd) {
            ALOGE("%s:%d: Failed to allocated memory", _FILE,
                  __LINE__);
            break;
        }

        // Wait for a client to connect.
        *fd = accept(server.sockfd, (struct sockaddr *)&caddr, &caddr_len);

        if (-1 == *fd) {
            ALOGE("%s:%d: Connection not accepted", _FILE,
                  __LINE__);
            free(fd);
            continue;
        }

        // Check if the maximum number of connected clients has been reached.
        if (accept_connection()) {
            // Create a client connection handler.
            if (pthread_create(&conn, NULL, client_thread, fd) != 0) {
                ALOGE("%s:%d: Failed to create client connection thread",
                      _FILE, __LINE__);
                free(fd);
            }
        } else {
            ALOGD("%s:%d: Max number of connections reached", _FILE,
                  __LINE__);
            free(fd);
        }
    }

    ALOGD("%s:%d: Exit server thread", _FILE, __LINE__);
    close(server.sockfd);
    set_server_status(STOPPED);

    return NULL;
}

/**
 * @brief Handle the communication with a connected client.
 *
 * @param [in] arg Pointer to a client socket file descriptor.
 *
 * @return Returns NULL at thread exit.
 */
static void * client_thread(void *arg)
{
    int rc;
    int *fd;
    ssize_t bytes_read;
    char command[CMD_LINE_LENGTH + 1];
    char response[CMD_LINE_LENGTH + 1];

    // Get socket file desc.
    fd = (int *)arg;

    // Keep the resp buffer terminated.
    response[CMD_LINE_LENGTH] = '\0';

    inc_ref_count();

    ALOGD("%s:%d: Enter client thread", _FILE, __LINE__);

    while (1) {
        // Wait for a client command.
        bytes_read = recv_line(*fd, command, CMD_LINE_LENGTH);

        if (0 == bytes_read) {
            ALOGD("%s:%d: Connection closed by peer", _FILE, __LINE__);
            break;
        } else if (bytes_read < 0) {
            ALOGD("%s:%d: Connection error (errno=%d)", _FILE, __LINE__,
                  errno);
            break;
        } else {
            // Message received (remove line feed character).
            command[bytes_read - 1] = '\0';

            // Clear response string.
            strncpy(response, NULL_STR, CMD_LINE_LENGTH);

            // Dispatch the message to a valid handler and send back response.
            rc = dispatch_command(command, response, CMD_LINE_LENGTH);
            if (send_response(*fd, rc, response, CMD_LINE_LENGTH) == -1) {
                break;
            }
        }
    }

    ALOGD("%s:%d: Exit client thread", _FILE, __LINE__);

    dec_ref_count();
    close(*fd);
    free(fd);

    return NULL;
}

/**
 * @brief Get server status
 *
 * @return Returns the status of the server.
 */
static enum status get_server_status(void)
{
    enum status status;

    pthread_mutex_lock(&mutex);
    status = server.status;
    pthread_mutex_unlock(&mutex);

    return status;
}

/**
 * @brief Set server status.
 *
 * @param [in] status Status to be set.
 */
static void set_server_status(enum status status)
{
    pthread_mutex_lock(&mutex);
    server.status = status;
    pthread_mutex_unlock(&mutex);
}

/**
 * @brief Check if the connection request can be accepted.
 *
 * @return Returns 1 if accepted, otherwise 0.
 */
static int accept_connection(void)
{
    int accept = 0;

    pthread_mutex_lock(&mutex);
    if (client.ref_count < MAX_CONNECTED_CLIENTS) {
        accept = 1;
    }
    pthread_mutex_unlock(&mutex);

    return accept;
}

/**
 * @brief Increase the client reference counter.
 */
static void inc_ref_count(void)
{
    pthread_mutex_lock(&mutex);
    client.ref_count++;
    pthread_mutex_unlock(&mutex);
}

/**
 * @brief Decrease the client reference counter.
 */
static void dec_ref_count(void)
{
    pthread_mutex_lock(&mutex);
    if (client.ref_count > 0) {
        client.ref_count--;
    }
    pthread_mutex_unlock(&mutex);
}

/**
 * @brief Dispatches the command to the correct sub-handler.
 *
 * @param [in]  cmd  Command string.
 * @param [out] resp Response buffer.
 * @param [in]  len  Length of response buffer.
 *
 * @return Returns 0 at success, or -1 at failure.
 */
static int dispatch_command(const char *cmd, char *resp, uint32_t len)
{
    int rc = -1;

    // Remove leading whitespace.
    while (isspace(*cmd)) {
        cmd++;
    }

    // Check for empty command.
    if ('\0' == *cmd) {
        ALOGE("%s:%d: No command found", _FILE, __LINE__);
        return -1;
    }

    // Dispatch command-line to correct handler.
    if (strncmp(cmd, TRACE_CMD, strlen(TRACE_CMD)) == 0) {
        rc = tracecmd_exec(cmd, resp, len);
    }

    return rc;
}

/**
 * @brief Receive line from the socket.
 *
 * @param [in]  fd   Socket file descriptor.
 * @param [out] line Destination buffer.
 * @param [in]  size size of destination buffer.
 *
 * @return Returns the number of bytes received, or -1 on failure. The return
 *         value will be 0 if the peer has shutdown.
 */
static int recv_line(int fd, char *line, uint32_t size)
{
    int n, bytes_read = 0;
    const uint32_t recv_len = 1;

    if (size < 1) {
        ALOGE("%s:%d: BUffer size is 0", _FILE, __LINE__);
        return -1;
    }

    while (1) {
        if ((bytes_read + recv_len) <= size) {
            n = recv(fd, &line[bytes_read], recv_len, 0);
            if (n > 0) {
                if (ASCII_LF == line[bytes_read]) {
                    return (bytes_read + n);
                }
                bytes_read += n;
            } else {
                return n;
            }
        } else {
            bytes_read = 0;
        }
    }
}

/**
 * @brief Send a buffer on the socket.
 *
 * @param [in] fd   Socket file descriptor.
 * @param [in] buf  Data source buffer.
 * @param [in] size Data size
 *
 * @return Returns 0 on success and -1 on failure.
 */
static int send_buf(int fd, const char *buf, uint32_t size)
{
    uint32_t bytes_sent = 0;
    uint32_t bytes_left = size;
    int n = 0;

    while (bytes_sent < size) {
        n = send(fd, buf + bytes_sent, bytes_left, 0);

        if (-1 == n) {
            ALOGE("%s:%d: Failed to send (errno=%d)", _FILE,  __LINE__,
                  errno);
            break;
        }

        bytes_sent += n;
        bytes_left -= n;
    }

    return ((-1 == n) ? -1 : 0);
}

/**
 * @brief Send response to a received command.
 *
 * @param [in]     fd     Socket file descriptor.
 * @param [in]     status Command execution status.
 * @param [in out] resp   Response buffer containing null-terminated string.
 * @param [in]     size   Size of response buffer.
 *
 * @return Returns 0 on success and -1 on failure.
 */
static int send_response(int fd, int status, char *resp, uint32_t size)
{
    if (-1 == status) {
        if (send_buf(fd, RES_KO, strlen(RES_KO)) == -1) {
            return -1;
        }
    } else {
        if (strcmp(resp, NULL_STR) != 0) {
            // Send response string.
            if (strlen(resp) + strlen(LINE_END) < size) {
                strcat(resp, LINE_END);
                if (send_buf(fd, resp, strlen(resp)) == -1) {
                    return -1;
                }
            }
        }

        if (send_buf(fd, RES_OK, strlen(RES_OK)) == -1) {
            return -1;
        }
    }

    return 0;
}
