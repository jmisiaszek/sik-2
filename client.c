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
#include <stdbool.h>

#include "err.h"
#include "common.h"

char *hostname;
uint16_t port;
int family = AF_UNSPEC;
char game_side;
bool is_automatic = false;

char game_type;
chat starting_player;
card_t cards[13];

// Function to parse command line arguments.
void parse_args(int argc, char *argv[]) {
    // Necessary arguments to be passed.
    bool hostname_set = false;
    bool port_set = false;
    bool game_side_set = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            if (i + 1 == argc) {
                fatal("Missing hostname");
            }
            hostname = argv[i + 1];
            hostname_set = true;
            i++;
        } else if (strcmp(argv[i], "-p") == 0) {
            if (i + 1 == argc) {
                fatal("Missing port number");
            }
            port = read_port(argv[i + 1]);
            port_set = true;
            i++;
        } else if (strcmp(argv[i], "-4") == 0) {
            family = AF_INET;
        } else if (strcmp(argv[i], "-6") == 0) {
            family = AF_INET6;
        } else if (strcmp(argv[i], "-a") == 0) {
            is_automatic = true;
        } else if (strcmp(argv[i], "-N") == 0) {
            game_side = 'N';
            game_side_set = true;
        } else if (strcmp(argv[i], "-E") == 0) {
            game_side = 'E';
            game_side_set = true;
        } else if (strcmp(argv[i], "-S") == 0) {
            game_side = 'S';
            game_side_set = true;
        } else if (strcmp(argv[i], "-W") == 0) {
            game_side = 'W';
            game_side_set = true;
        } else {
            fatal("Invalid argument: %s", argv[i]);
        }
    }
    if (!hostname_set ||!port_set ||!game_side_set) {
        fatal("Missing obligatory argument");
    }
}

int prepare_connection() {
    // Get server address info.
    struct sockaddr_storage server_address = get_server_address(hostname, port, family);
    family = server_address.ss_family;

    // Create socket.
    int socket_fd = socket(family, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        syserr("cannot create a socket");
    }

    if (connect(socket_fd, (struct sockaddr *) &server_address,
                (socklen_t) sizeof(server_address)) < 0) {
        syserr("cannot connect to the server");
    }

    /*DEBUG*/printf("connected to %s:%" PRIu16 "\n", hostname, port);

    return socket_fd;
}

int handshake(int socket_fd) {
    // Preparing and sending IAM message.
    size_t msg_len = 6;
    char *msg = malloc((msg_len + 1) * sizeof(char));
    memset(msg, 0, (msg_len + 1) * sizeof(char));
    strcpy(msg, "IAM");
    msg[3] = game_side;
    strcat(msg, "\r\n");
    // TODO: Raport.

    ssize_t written_length = writen(socket_fd, msg, msg_len);
    if (written_length < 0) {
        syserr("writen");
    }
    else if ((size_t) written_length != msg_len) {
        fatal("incomplete writen");
    }
    free(msg);

    return get_game_info(socket_fd);
}

int get_game_info(int socket_fd) {
    // Receiving DEAL message.
    msg = read_msg(socket_fd);
    msg_len = strlen(msg);
    
    // TODO: Raport.

    if (msg_len < 2 || msg[msg_len - 2] != '\r' || msg[msg_len - 1] != '\n') {
        free(msg);
        return get_game_info(socket_fd);
    } else {
        if (strncmp(msg, "BUSY", 4) == 0) {
            return 1;
        } else if (strncmp(msg, "DEAL", 4) == 0) {
            // Fill game data.
            game_type = msg[4];
            starting_player = msg[5];
            size_t ptr = 5;
            for (int i = 0; i < 13; i++) {
                cards[i].num = msg[++ptr];
                if (msg[ptr] == '1') {
                    ptr++;
                }
                cards[i].col = msg[++ptr];
            }

            return 0;
        } else {
            return get_game_info(socket_fd);
        }
    }
}

void auto_play(int socket_fd) {

}

void manual_play(int socket_fd) {

}

int main(int argc, char *argv[]) {
    parse_args(argc, argv);
    
    int socket_fd = prepare_connection();

    handshake(socket_fd);


}