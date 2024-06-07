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

#define IAM_LEN 6
#define TRICK_LEN 10

#include "err.h"
#include "common.h"

// Command line arguments.
char *hostname;
uint16_t port;
int family = AF_UNSPEC;
char game_side;
bool is_automatic = false;

// Game data.
char game_type;
char starting_player;
card_t cards[13];

// Function to parse command line arguments.
static void parse_args(int argc, char *argv[]) {
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

// Function to initialize connection to the server.
static int prepare_connection() {
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

// Function to get game data from the server.
static int get_game_info(int socket_fd) {
    // Receiving DEAL message.
    char *msg = read_msg(socket_fd);
    int msg_len = strlen(msg);
    
    // TODO: Raport.
    printf("%s\n", msg);

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

// Function to exchange initial data with the server.
static int handshake(int socket_fd) {
    // Preparing and sending IAM message.
    size_t msg_len = IAM_LEN;
    char *msg = malloc((msg_len + 1) * sizeof(char));
    if (msg == NULL) {
        syserr("malloc");
    }
    memset(msg, 0, (msg_len + 1) * sizeof(char));
    strcpy(msg, "IAM");
    msg[strlen(msg)] = game_side;
    strcat(msg, "\r\n");

    // TODO: Raport.
    printf("%s\n", msg);

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

// Converts card number to proper array index (automatic play).
static int numtoi(char num) {
    if (num == '1') {
        return 8;
    } else if (num == 'J') {
        return 9;
    } else if (num == 'Q') {
        return 10;
    } else if (num == 'K') {
        return 11;
    } else if (num == 'A') {
        return 12;
    } else {
        return num - '0' - 2;
    }
}

// Plays out the hand automatically.
static void auto_play(int socket_fd) {
    char *msg;
    size_t msg_len;
    bool correct_packet;

    // Playing out all tricks.
    for (int t = 1; t <= 13; t++) {
        // Read info about the current trick.
        correct_packet = false;
        while (!correct_packet) {
            msg = read_msg(socket_fd);
            msg_len = strlen(msg);

            // TODO: Raport.
            printf("%ld %s\n", msg_len, msg);

            if (msg_len < 2 || msg[msg_len - 2] != '\r' || 
                msg[msg_len - 1] != '\n') {
                free(msg);
            } else if (strncmp(msg, "TRICK", 5) == 0) {
                correct_packet = true;
            } else {
                free(msg);
            }
        }

        // Pick a card to play. Strategy is to always play the lowest if
        // possible to win and the highest otherwise.
        
        card_t laid_cards[3]; // Max number of cards in a trick.
        // Fill info about the cards in trick.
        int ptr = 5; // Msg after "TRICK".
        char trick_num = msg[ptr++];
        char trick_second_num;
        if (t >= 10) {
            trick_second_num = msg[ptr++];
        }
        int card = 0;
        while (msg[ptr]!= '\r') {
            laid_cards[card].num = msg[ptr++];
            if (msg[ptr] == '0') {
                ptr++;
            }
            laid_cards[card].col = msg[ptr++];
            card++;
        }
        free(msg);
        
        card_t card_to_play;
        card_to_play.num = -1;
        card_to_play.col = -1;
        int card_id = -1;
        if (card == 0) { // No cards in the trick. We pick lowest possible card.
            for(int i = 0; i < 13; i++) {
                if (cards[i].num == -1) {
                    continue;
                } else if (card_to_play.num == -1) {
                    card_to_play = cards[i];
                    card_id = i;
                } else if (numtoi(cards[i].num) < numtoi(card_to_play.num)) {
                    card_to_play = cards[i];
                    card_id = i;
                }
            }
        } else {
            // Find the highest card in the trick.
            card_t highest_card = laid_cards[0];
            for (int i = 1; i < card; i++) {
                if (highest_card.col == laid_cards[i].col && 
                    numtoi(laid_cards[i].num) > numtoi(highest_card.num)) {
                    highest_card = laid_cards[i];
                }
            }
            // Chceck if we can play lower card.
            for (int i = 0; i < 13; i++) {
                if (cards[i].num == -1) {
                    continue;
                } else if (cards[i].col == highest_card.col && 
                    numtoi(cards[i].num) < numtoi(highest_card.num)) {
                    card_to_play = cards[i];
                    card_id = i;
                }
            }
            if (card_id == -1) {
                // If we can't play lower card, we play the highest card.
                for (int i = 0; i < 13; i++) {
                    if (cards[i].num == -1) {
                        continue;
                    } else if (card_to_play.num == -1) {
                        card_to_play = cards[i];
                        card_id = i;
                    } else if (numtoi(cards[i].num) > numtoi(card_to_play.num)) {
                        card_to_play = cards[i];
                        card_id = i;
                    }
                }
            }
        }

        // Send card info to the server.
        msg_len = TRICK_LEN;
        if (card_to_play.num == '1') {
            msg_len++;
        }
        if (t >= 10) {
            msg_len++;
        }
        msg = malloc((msg_len + 1) * sizeof(char));
        if (msg == NULL) {
            syserr("malloc");
        }
        memset(msg, 0, (msg_len + 1) * sizeof(char));
        strcpy(msg, "TRICK");
        msg[strlen(msg)] = trick_num;
        if (t >= 10) {
            msg[strlen(msg)] = trick_second_num;
        }
        msg[strlen(msg)] = card_to_play.num;
        if (card_to_play.num == '1') {
            msg[strlen(msg)] = '0';
        }
        msg[strlen(msg)] = card_to_play.col;
        strcat(msg, "\r\n");

        // TODO: Raport.
        printf("%ld %s\n", msg_len, msg);

        ssize_t written_length = writen(socket_fd, msg, msg_len);
        if (written_length < 0) {
            syserr("writen");
        }
        else if ((size_t) written_length != msg_len) {
            fatal("incomplete writen");
        }
        free(msg);

        cards[card_id].num = -1;
        cards[card_id].col = -1;

        // Receive info about who took the trick.
        correct_packet = false;
        while (!correct_packet) {
            msg = read_msg(socket_fd);
            msg_len = strlen(msg);

            // TODO: Raport.
            printf("%s\n", msg);

            if (msg_len < 2 || msg[msg_len - 2] != '\r' || 
                msg[msg_len - 1] != '\n') {
                free(msg);
            } else if (strncmp(msg, "TAKEN", 5) == 0) {
                correct_packet = true;
            } else {
                free(msg);
            }
        }
        free(msg);
    }

    // Receive info about who won the trick.
    correct_packet = false;
    while (!correct_packet) {
        msg = read_msg(socket_fd);
        msg_len = strlen(msg);

        // TODO: Raport.
        printf("%s\n", msg);

        if (msg_len < 2 || msg[msg_len - 2] != '\r' || 
            msg[msg_len - 1] != '\n') {
            free(msg);
        } else if (strncmp(msg, "SCORE", 5) == 0) {
            correct_packet = true;
        } else {
            free(msg);
        }
    }
    free(msg);

    // Receive info about total amount of points.
    correct_packet = false;
    while (!correct_packet) {
        msg = read_msg(socket_fd);
        msg_len = strlen(msg);

        // TODO: Raport.
        printf("%s\n", msg);

        if (msg_len < 2 || msg[msg_len - 2] != '\r' || 
            msg[msg_len - 1] != '\n') {
            free(msg);
        } else if (strncmp(msg, "TOTAL", 5) == 0) {
            correct_packet = true;
        } else {
            free(msg);
        }
    }
    free(msg);
}

/*
static void manual_play(int socket_fd) {

}
*/

int main(int argc, char *argv[]) {
    parse_args(argc, argv);
    
    int socket_fd = prepare_connection();

    handshake(socket_fd);

    while (true) {
        if (is_automatic) {
            while (true) {
                auto_play(socket_fd);

                // Check if there is more data available.
                char *temp = malloc(sizeof(char) * 2);
                ssize_t read_length = recv(socket_fd, temp, 1, MSG_PEEK);
                if (read_length < 0) {
                    syserr("recv");
                } else if (read_length == 0) {
                    close(socket_fd);
                    exit(0);
                }
                free(temp);

                get_game_info(socket_fd);
            }
        } else {
            //manual_play(socket_fd);
        }
    }
}