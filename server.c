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
#include <poll.h>

#include "err.h"
#include "common.h"

#define QUEUE_LENGTH 5

typedef struct card_t {
    char num;
    char col;
} card_t;

typedef struct game_desc_t {
    int game_type;
    char starting_player;
    card_t cards[4][13];
} game_desc_t;

const int MAX_CONNECTIONS = 5; // Server + 4 players.

uint16_t port = 0;
char *game_file = NULL;
time_t timeout = 5;

int no_of_games;
game_desc_t *game_desc;


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

int count_lines() {
    FILE *file = fopen(game_file, "r");
    if (file == NULL) {
        syserr("Failed to open game description file.");
    }

    int line_count = 0;
    char ch;

    // Count the number of newline characters
    while ((ch = fgetc(file)) != EOF) {
        if (ch == '\n') {
            line_count++;
        }
    }

    fclose(file);

    return line_count;
}

void parse_game_file() {
    no_of_games = count_lines() / 5;
    
    game_desc = malloc(no_of_games * sizeof(game_desc_t));

    FILE *file = fopen(game_file, "r");
    if (file == NULL) {
        syserr("Failed to open game description file.");
    }

    for(int i = 0; i < no_of_games; i++) {
        game_desc[i].game_type = (int)(fgetc(file) - '0');
        game_desc[i].starting_player = fgetc(file);
        fgetc(file); // Skip the newline character.
        for (int j = 0; j < 4; j++) {
            for(int k = 0; k < 13; k++) {
                game_desc[i].cards[j][k].num = fgetc(file);
                if (game_desc[i].cards[j][k].num == '1') {
                    fgetc(file); // Skip the '0' character.
                }
                game_desc[i].cards[j][k].col = fgetc(file);
            }
            fgetc(file); // Skip the newline character.
        }
    }

    fclose(file);
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

    /*DEBUG*/printf("listening on port %" PRIu16 "\n", ntohs(server_address_actual.sin6_port));
    return socket_fd;
}

int main(int argc, char *argv[]) {
    parse_args(argc, argv);

    parse_game_file();

    int server_fd = prepare_connection();

    /*while(1) {
        struct sockaddr_in client_address;
        int client_fd = accept(server_fd, (struct sockaddr *) &client_address,
                               &((socklen_t){sizeof(client_address)}));
        if (client_fd < 0) {
            syserr("accept");
        }

        char const *client_ip = inet_ntoa(client_address.sin_addr);
        uint16_t client_port = ntohs(client_address.sin_port);
        printf("accepted connection from %s:%" PRIu16 "\n", client_ip, client_port);

        close(client_fd);
    }*/
}