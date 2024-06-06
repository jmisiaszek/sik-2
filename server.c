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

uint16_t port = -1;
char *game_file = NULL;
time_t timeout = 5;

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

int main(int argc, char *argv[]) {
    parse_args(argc, argv);
    printf("Port: %d\n", port);
    printf("Timeout: %ld\n", timeout);
    printf("Game file: %s\n", game_file);
}