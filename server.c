#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdbool.h>

#include "err.h"
#include "common.h"

#define QUEUE_LENGTH 5

uint16_t port = 0;
char *game_file = NULL;
time_t timeout = 5;

//  TODO: implement SIGINT handling.
/*static void catch_int(int sig) {
    finish = true;
    printf("signal %d catched so no new connections will be accepted\n", sig);
}*/

// Function to parse command line arguments.
void parse_args(int argc, char *argv[]) {
    bool file_set = false;
    for (int i = 1; i < argc; i += 2) {
        if (strcmp(argv[i], "-p") == 0) {
            if (i + 1 == argc) {
                fatal("No port specified.\n");
            }
            port = read_port(argv[i+1]);
        } else if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 == argc) {
                fatal("No timeout specified.\n");
            }
            timeout = read_time(argv[i+1]);
        } else if (strcmp(argv[i], "-f") == 0) {
            if (i + 1 == argc) {
                fatal("No game file specified.\n");
            }
            file_set = true;
            game_file = argv[i+1];
        } else {
            fatal("Invalid argument: %s\n", argv[i]);
        }
    }
    if (!file_set) {
        fatal("No game file specified.\n");
    }
}

int prepare_connection() {
    // Create an IPv6 socket.
    int socket_fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        syserr("cannot create a socket");
    }
    
    // Allow the socket to accept both IPv4 and IPv6 connections
    int option = 0;
    if (setsockopt(socket_fd, IPPROTO_IPV6, IPV6_V6ONLY, &option, sizeof(option)) < 0) {
        syserr("setsockopt");
    }

    // Bind the socket to a concrete address.
    struct sockaddr_in6 server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin6_family = AF_INET6; // IPv6
    server_address.sin6_addr = in6addr_any; // Listening on all interfaces.
    server_address.sin6_port = htons(port); // If port is 0, the kernel will choose a port.

    if (bind(socket_fd, (struct sockaddr *) &server_address, (socklen_t) sizeof server_address) < 0) {
        syserr("bind");
    }

    // Switch the socket to listening.
    if (listen(socket_fd, QUEUE_LENGTH) < 0) {
        syserr("listen");
    }

    // Find out what port the server is actually listening on.
    struct sockaddr_in6 server_address_actual;
    socklen_t length = sizeof(server_address_actual);
    if (getsockname(socket_fd, (struct sockaddr *) &server_address_actual, &length) < 0) {
        syserr("getsockname");
    }

    printf("listening on port %" PRIu16 "\n", ntohs(server_address_actual.sin6_port));
    return socket_fd;
}

int main(int argc, char *argv[]) {
    parse_args(argc, argv);
    
    // TODO: install_signal_handler(SIGINT, catch_int, SA_RESTART);

    int socket_fd = prepare_connection();
}