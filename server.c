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
#include <sys/time.h>
#include <stdbool.h>
#include <poll.h>

#include "err.h"
#include "common.h"

#define QUEUE_LENGTH 5
#define NO_PLAYERS 4
#define POLL_SIZE 9
#define NO_TRICKS 13

#define N 1
#define E 2
#define S 3
#define W 4

// Struct to store socket and last activity time.
typedef struct socket_info_t {
    int fd;
    time_t last_activity;
} socket_info_t;

// Function to get current time
static time_t current_time() {
    return time(NULL);
}

// Function to calculate inactivity duration
static double calculate_inactivity_duration(time_t last_activity_time) {
    return difftime(current_time(), last_activity_time);
}

// Struct to store information about game.
typedef struct game_desc_t {
    char game_type;
    char starting_player;
    card_t cards[NO_PLAYERS][NO_TRICKS];
} game_desc_t;

// Variables to store command line arguments.
uint16_t port = 0;
char *game_file = NULL;
time_t timeout = 5;

// Variables to store information about games.
int no_of_games;
game_desc_t *game_desc;
int current_game = 0;
int current_trick = 0;
int ready_players = 0;
int current_player = 0;
card_t cards_played[NO_TRICKS][NO_PLAYERS];
int who_played[NO_PLAYERS]; // Values are from 1 to NO_PLAYERS.
int total_points[NO_PLAYERS];
int points[NO_PLAYERS];
int who_took_trick = 0;

// Poll structure.
struct pollfd poll_fds[POLL_SIZE];
socket_info_t poll_fds_info[POLL_SIZE];

// Function to parse command line arguments.
static void parse_args(int argc, char *argv[]) {
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

// Function to count lines of game description file.
static int count_lines() {
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

// Function to parse game description file.
static void parse_game_file() {
    no_of_games = count_lines() / 5;
    
    game_desc = malloc(no_of_games * sizeof(game_desc_t));

    FILE *file = fopen(game_file, "r");
    if (file == NULL) {
        syserr("Failed to open game description file.");
    }

    for(int i = 0; i < no_of_games; i++) {
        game_desc[i].game_type = fgetc(file);
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

// Function to initialize the server socket.
static int prepare_connection() {
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

// Function to print raport about exchanged messages.
static void raport(int socket_fd, char *msg, bool from_client) {
    // Get local IP address and port
    struct sockaddr_in local_address;
    socklen_t local_address_len = sizeof(local_address);
    if (getsockname(socket_fd, (struct sockaddr *) &local_address, &local_address_len) == -1) {
        syserr("getsockname");
    }
    char local_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &local_address.sin_addr, local_ip, sizeof(local_ip));
    int local_port = ntohs(local_address.sin_port);

    // Get server IP address and port
    struct sockaddr_in server_address;
    socklen_t server_address_len = sizeof(server_address);
    if (getpeername(socket_fd, (struct sockaddr *) &server_address, &server_address_len) == -1) {
        syserr("getpeername");
    }
    char server_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &server_address.sin_addr, server_ip, sizeof(server_ip));
    int server_port = ntohs(server_address.sin_port);

    // Get the current time
    struct timeval tv;
    gettimeofday(&tv, NULL);

    // Convert it to local time representation.
    struct tm *local_time = localtime(&tv.tv_sec);
    if (local_time == NULL) {
        syserr("localtime");
    }

    // Print the current date and time to string.
    char time_str[100];
    if (strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", local_time) == 0) {
        syserr("strftime");
    }

    // Print the whole raport.
    msg[strlen(msg) - 2] = '\0';
    if (from_client) {
        printf("[%s:%d,%s:%d,%s.%03ld] %s\\r\\n\n", server_ip, server_port, 
            local_ip, local_port, time_str, tv.tv_usec / 1000, msg);
    } else {
        printf("[%s:%d,%s:%d,%s.%03ld] %s\\r\\n\n", local_ip, local_port, 
            server_ip, server_port, time_str, tv.tv_usec / 1000, msg);
    }
    msg[strlen(msg)] = '\r';
}

// Function to check if a place for player is free.
static int check_for_place(int client_fd, char place) {
    int place_id;
    if (place == 'N') {
        place_id = N;
    } else if (place == 'E') {
        place_id = E;
    } else if (place == 'S') {
        place_id = S;
    } else if (place == 'W') {
        place_id = W;
    } else {
        return -1;
    }
    if (poll_fds[place_id].fd != -1) {
        // Send BUSY message.
        char *msg = malloc(BUF_SIZE * sizeof(char));
        memset(msg, 0, BUF_SIZE * sizeof(char));
        strcat(msg, "BUSY");
        if (poll_fds[N].fd == -1) {
            strcat(msg, "N");
        } else if (poll_fds[E].fd == -1) {
            strcat(msg, "E");
        } else if (poll_fds[S].fd == -1) {
            strcat(msg, "S");
        } else if (poll_fds[W].fd == -1) {
            strcat(msg, "W");
        }
        strcat(msg, "\r\n");

        raport(client_fd, msg, false);

        ssize_t written_length = writen(client_fd, msg, strlen(msg));
        if (written_length < 0) {
            syserr("writen");
        }
        else if ((size_t) written_length != strlen(msg)) {
            fatal("incomplete writen");
        }
        free(msg);
        return -1;
    }  else {
        // Update poll structure.
        poll_fds[place_id].fd = client_fd;
        poll_fds[place_id].events = POLLIN;
        poll_fds_info[place_id].fd = client_fd;
        poll_fds_info[place_id].last_activity = current_time();

        // Send game data.
        char *msg = malloc(BUF_SIZE * sizeof(char));
        memset(msg, 0, BUF_SIZE * sizeof(char));
        strcat(msg, "DEAL");
        msg[strlen(msg)] = game_desc[current_game].game_type;
        msg[strlen(msg)] = game_desc[current_game].starting_player;
        for (int i = 0; i < NO_TRICKS; i++) {
            msg[strlen(msg)] = game_desc[current_game].cards[place_id - 1][i].num;
            if (game_desc[current_game].cards[place_id - 1][i].num == '1') {
                msg[strlen(msg)] = '0';
            }
            msg[strlen(msg)] = game_desc[current_game].cards[place_id - 1][i].col;
        }
        strcat(msg, "\r\n");

        raport(client_fd, msg, false);

        ssize_t written_length = writen(client_fd, msg, strlen(msg));
        if (written_length < 0) {
            syserr("writen");
        }
        else if ((size_t) written_length != strlen(msg)) {
            fatal("incomplete writen");
        }
        free(msg);
        ready_players++;
        return 0;
    }
}

// Function to wait for clients to connect.
static void get_players() {
    while (ready_players < NO_PLAYERS) {
        // Clear poll revents.
        for (int i = 0; i < POLL_SIZE; i++) {
            poll_fds[i].revents = 0;
        }

        // Wait for a client to connect.
        int ret = poll(poll_fds, POLL_SIZE, timeout * 1000);
        if (ret < 0) {
            syserr("poll");
        } else {
            // Check if the server socket received a connection.
            if (poll_fds[0].revents & POLLIN) {
                // New connection: new client is accepted.
                struct sockaddr_storage client_address;
                int client_fd = accept(poll_fds[0].fd,
                                       (struct sockaddr *) &client_address,
                                       &((socklen_t){sizeof client_address}));
                if (client_fd < 0) {
                    syserr("accept");
                }

                for (int i = NO_PLAYERS + 1; i < POLL_SIZE; i++) {
                    if (poll_fds[i].fd == -1) {
                        poll_fds[i].fd = client_fd;
                        poll_fds[i].events = POLLIN;
                        poll_fds_info[i].fd = client_fd;
                        poll_fds_info[i].last_activity = current_time();
                        break;
                    }
                }
            }

            // Chceck the awaiting clients for input.
            for (int i = NO_PLAYERS + 1; i < POLL_SIZE; i++) {
                if (poll_fds[i].fd != -1) {
                    if (poll_fds[i].revents & POLLIN) {
                        // Receive input from the client.
                        char *msg = read_msg(poll_fds[i].fd);
                        if (msg == NULL) {
                            // Client disconnected.
                            printf("Client disconnected\n");
                            close(poll_fds[i].fd);
                            poll_fds[i].fd = -1;
                            poll_fds_info[i].fd = -1;
                            continue;
                        }

                        raport(poll_fds[i].fd, msg, true);

                        if (strncmp(msg, "IAM", 3) == 0 && 
                            strlen(msg) == strlen("IAM") + strlen("\r\n") + 1) {
                            char place = msg[3];
                            int code = check_for_place(poll_fds[i].fd, place);
                            if (code == -1) {
                                close(poll_fds[i].fd);
                            }
                            poll_fds[i].fd = -1;
                            poll_fds_info[i].fd = -1;
                        } else {
                            close(poll_fds[i].fd);
                            poll_fds[i].fd = -1;
                            poll_fds_info[i].fd = -1;
                        }
                    } else {
                        // Check for timeout.
                        if (calculate_inactivity_duration
                            (poll_fds_info[i].last_activity) > timeout) {
                            close(poll_fds[i].fd);
                            poll_fds[i].fd = -1;
                            poll_fds_info[i].fd = -1;
                        }
                    }
                }
            }
        }
    }
}

// Function to send "TRICK" message.
static void send_trick() {
    // Send the trick.
    char *msg = malloc(BUF_SIZE * sizeof(char));
    memset(msg, 0, BUF_SIZE * sizeof(char));
    strcat(msg, "TRICK");
    char num[15];
    sprintf(num, "%d", current_trick + 1);
    strcat(msg, num);
    for (int i = 0; i < NO_PLAYERS; i++) {
        if (cards_played[current_trick][i].num!= 0) {
            msg[strlen(msg)] = cards_played[current_trick][i].num;
            if (cards_played[current_trick][i].num == '1') {
                msg[strlen(msg)] = '0';
            }
            msg[strlen(msg)] = cards_played[current_trick][i].col;
        }
    }
    strcat(msg, "\r\n");

    raport(poll_fds[current_player].fd, msg, false);

    ssize_t written_length = writen(poll_fds[current_player].fd, msg, strlen(msg));
    if (written_length < 0) {
        syserr("writen");
    }
    else if ((size_t) written_length != strlen(msg)) {
        fatal("incomplete writen");
    }
    free(msg);
}

// Function to parse a "TRICK" message and react accordingly.
static int parse_trick(char *msg) {
    // Parse the message.
    int ptr = strlen(msg) - 1 - strlen("\r\n");
    char col = msg[ptr--];
    char num = msg[ptr--];
    if (num == '0' && msg[ptr] == '1') {
        num = msg[ptr--];
    }
    int trick_num = 0;
    for (int i = strlen("TRICK"); i <= ptr; i++) {
        trick_num = trick_num * 10 + (msg[i] - '0');
    }

    // Check if the trick is valid.
    // Check if the player has a card in the color of first card.
    bool has_color = false;
    if (cards_played[current_trick][0].num != 0) {
        for (int i = 0; i < NO_TRICKS; i++) {
            if (game_desc[current_game].cards[current_player - 1][i].col 
                == cards_played[current_trick][0].col) {
                printf("%c %c\n", game_desc[current_game].cards[current_player - 1][i].num, game_desc[current_game].cards[current_player - 1][i].col);
                has_color = true;
                break;
            }
        }
    }
    if (trick_num != current_trick + 1) {
        return -1;
    } else if (has_color && col != cards_played[current_trick][0].col) {
        printf("Invalid trick\n");
        return -1;
    } else {
        for (int i = 0; i < NO_TRICKS; i++) {
            if (game_desc[current_game].cards[current_player - 1][i].num == num &&
                game_desc[current_game].cards[current_player - 1][i].col == col &&
                num != 0 && col != 0) {
                int card_id = 0;
                while (cards_played[current_trick][card_id].num != 0) {
                    card_id++;
                }
                cards_played[current_trick][card_id].num = num;
                cards_played[current_trick][card_id].col = col;
                game_desc[current_game].cards[current_player - 1][i].num = 0;
                game_desc[current_game].cards[current_player - 1][i].col = 0;
                who_played[card_id] = current_player;
                current_player = current_player % 4 + 1;
                return 0;
            }
        }
        return -1;
    }
}

// Function to determine who took the trick.
static int resolve() {
    char col = cards_played[current_trick][0].col;
    char num = cards_played[current_trick][0].num;
    int who_took = who_played[0];
    for (int i = 1; i < NO_PLAYERS; i++) {
        if (cards_played[current_trick][i].col == col && 
            numtoi(cards_played[current_trick][i].num) > numtoi(num)) {
            num = cards_played[current_trick][i].num;
            who_took = who_played[i];
        }
    }
    if (game_desc[current_game].game_type == '1' || 
        game_desc[current_game].game_type == '7') {
        points[who_took - 1] += 1;
    } if (game_desc[current_game].game_type == '2' || 
        game_desc[current_game].game_type == '7') {
        for (int i = 0; i < NO_PLAYERS; i++) {
            if (cards_played[current_trick][i].col == 'H') {
                points[who_took - 1] += 1;
            }
        }
    } if (game_desc[current_game].game_type == '3' || 
        game_desc[current_game].game_type == '7') {
        for (int i = 0; i < NO_PLAYERS; i++) {
            if (cards_played[current_trick][i].num == 'Q') {
                points[who_took - 1] += 5;
            }
        }
    } if (game_desc[current_game].game_type == '4' || 
        game_desc[current_game].game_type == '7') {
        for (int i = 0; i < NO_PLAYERS; i++) {
            if (cards_played[current_trick][i].num == 'J' || 
                cards_played[current_trick][i].num == 'K') {
                points[who_took - 1] += 2;
            }
        }
    } if (game_desc[current_game].game_type == '5' || 
        game_desc[current_game].game_type == '7') {
        for (int i = 0; i < NO_PLAYERS; i++) {
            if (cards_played[current_trick][i].col == 'H' && 
                cards_played[current_trick][i].num == 'K') {
                points[who_took - 1] += 18;
            }
        }
    } if (game_desc[current_game].game_type == '6' || 
        game_desc[current_game].game_type == '7') {
        if (current_trick == 6 || current_trick == 12) {
            points[who_took - 1] += 10;
        }
    }
    return who_took;
}

// Function to send "TAKEN" message.
static void send_taken() {
    int who_took = resolve();
    char *msg = malloc(BUF_SIZE * sizeof(char));
    memset(msg, 0, BUF_SIZE * sizeof(char));
    strcat(msg, "TAKEN");
    char num[15];
    sprintf(num, "%d", current_trick + 1);
    strcat(msg, num);
    if (who_took == N) {
        msg[strlen(msg)] = 'N';
    } else if (who_took == E) {
        msg[strlen(msg)] = 'E';
    } else if (who_took == S) {
        msg[strlen(msg)] = 'S';
    } else {
        msg[strlen(msg)] = 'W';
    }
    strcat(msg, "\r\n");

    for (int i = 1; i <= NO_PLAYERS; i++) {
        raport(poll_fds[i].fd, msg, false);

        ssize_t written_length = writen(poll_fds[i].fd, msg, strlen(msg));
        if (written_length < 0) {
            syserr("writen");
        }
        else if ((size_t) written_length != strlen(msg)) {
            fatal("incomplete writen");
        }
    }
    free(msg);
    current_trick++;
    current_player = who_took;
}

// Sends the DEAL information to all clients.
static void send_new_deal() {
    // Send game data.
    char *msg = malloc(BUF_SIZE * sizeof(char));
    for (int place_id = 1; place_id <= NO_PLAYERS; place_id++) {
        memset(msg, 0, BUF_SIZE * sizeof(char));
        strcat(msg, "DEAL");
        msg[strlen(msg)] = game_desc[current_game].game_type;
        msg[strlen(msg)] = game_desc[current_game].starting_player;
        for (int i = 0; i < NO_TRICKS; i++) {
            msg[strlen(msg)] = game_desc[current_game].cards[place_id - 1][i].num;
            if (game_desc[current_game].cards[place_id - 1][i].num == '1') {
                msg[strlen(msg)] = '0';
            }
            msg[strlen(msg)] = game_desc[current_game].cards[place_id - 1][i].col;
        }
        strcat(msg, "\r\n");

        raport(poll_fds[place_id].fd, msg, false);

        ssize_t written_length = writen(poll_fds[place_id].fd, msg, strlen(msg));
        if (written_length < 0) {
            syserr("writen");
        }
        else if ((size_t) written_length != strlen(msg)) {
            fatal("incomplete writen");
        }
    }
    free(msg);
}

// Simulate the game.
static void play() {
    // Prepare values for the game.
    current_trick = 0;
    if (game_desc[current_game].starting_player == 'N') {
        current_player = N;
    } else if (game_desc[current_game].starting_player == 'E') {
        current_player = E;
    } else if (game_desc[current_game].starting_player == 'S') {
        current_player = S;
    } else {
        current_player = W;
    }
    for (int i = 0; i < NO_TRICKS; i++) {
        for (int j = 0; j < NO_PLAYERS; j++) {
            cards_played[i][j].num = 0;
            cards_played[i][j].col = 0;
        }
    }
    for (int i = 0; i < NO_PLAYERS; i++) {
        points[i] = 0;
    }

    // Send the first trick.
    send_trick();

    while (current_trick < NO_TRICKS) {
        if (ready_players < NO_PLAYERS) {
            get_players();
        }

        // Clear poll revents.
        for (int i = 0; i < POLL_SIZE; i++) {
            poll_fds[i].revents = 0;
        }

        // Wait for events.
        int ret = poll(poll_fds, POLL_SIZE, timeout * 1000);

        if (ret < 0) {
            syserr("poll");
        } else {
            if (poll_fds[0].revents & POLLIN) {
                // New connection: new client is accepted.
                struct sockaddr_storage client_address;
                int client_fd = accept(poll_fds[0].fd,
                                       (struct sockaddr *) &client_address,
                                       &((socklen_t){sizeof client_address}));
                if (client_fd < 0) {
                    syserr("accept");
                }

                char *msg = "BUSY\r\n";

                raport(client_fd, msg, false);

                ssize_t written_length = writen(client_fd, msg, strlen(msg));
                if (written_length < 0) {
                    syserr("writen");
                }
                else if ((size_t) written_length != strlen(msg)) {
                    fatal("incomplete writen");
                }
            }
            for (int i = 1; i <= NO_PLAYERS; i++) {
                if (poll_fds[i].fd != -1) {
                    if (poll_fds[i].revents & POLLIN) {
                        char *msg = read_msg(poll_fds[i].fd);
                        if (msg == NULL) {
                            // Client disconnected.
                            close(poll_fds[i].fd);
                            poll_fds[i].fd = -1;
                            poll_fds_info[i].fd = -1;
                            ready_players--;
                            continue;
                        }

                        raport(poll_fds[i].fd, msg, true);

                        if (i != current_player) {
                            if (strncmp(msg, "TRICK", 5) == 0) {
                                // Send the WRONG reply.
                                free(msg);
                                msg = malloc(BUF_SIZE * sizeof(char));
                                memset(msg, 0, BUF_SIZE * sizeof(char));
                                strcat(msg, "WRONG");
                                char num[15];
                                sprintf(num, "%d", current_trick + 1);
                                strcat(msg, num);
                                strcat(msg, "\r\n");

                                raport(poll_fds[i].fd, msg, false);

                                ssize_t written_length = writen(poll_fds[i].fd, msg, strlen(msg));
                                if (written_length < 0) {
                                    syserr("writen");
                                }
                                else if ((size_t) written_length != strlen(msg)) {
                                    fatal("incomplete writen");
                                }
                                free(msg);
                            } else {
                                // Disconnect the client.
                                close(poll_fds[i].fd);
                                poll_fds[i].fd = -1;
                                poll_fds_info[i].fd = -1;
                                ready_players--;
                            }
                        } else {
                            if (strncmp(msg, "TRICK", 5) != 0) {
                                // Disconnect the client.
                                close(poll_fds[i].fd);
                                poll_fds[i].fd = -1;
                                poll_fds_info[i].fd = -1;
                                ready_players--;
                            } else {
                                // Parse the trick.
                                int res = parse_trick(msg);
                                free(msg);
                                if (res == -1) {
                                    msg = malloc(BUF_SIZE * sizeof(char));
                                    memset(msg, 0, BUF_SIZE * sizeof(char));
                                    strcat(msg, "WRONG");
                                    char num[15];
                                    sprintf(num, "%d", current_trick + 1);
                                    strcat(msg, num);
                                    strcat(msg, "\r\n");

                                    raport(poll_fds[i].fd, msg, false);

                                    ssize_t written_length = writen(poll_fds[i].fd, msg, strlen(msg));
                                    if (written_length < 0) {
                                        syserr("writen");
                                    }
                                    else if ((size_t) written_length != strlen(msg)) {
                                        fatal("incomplete writen");
                                    }
                                    free(msg);
                                } else {
                                    if (cards_played[current_trick][NO_PLAYERS - 1].num != 0) {
                                        // Send the TAKEN message.
                                        send_taken();
                                    }
                                    if (current_trick < NO_TRICKS) {
                                        send_trick();
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    // Send the SCORE and TOTAL messages.
    char *msg = malloc(BUF_SIZE * sizeof(char));
    char num[15];
    memset(msg, 0, BUF_SIZE * sizeof(char));
    strcat(msg, "SCOREN");
    sprintf(num, "%d", points[0]);
    strcat(msg, num);
    msg[strlen(msg)] = 'E';
    sprintf(num, "%d", points[1]);
    strcat(msg, num);
    msg[strlen(msg)] = 'S';
    sprintf(num, "%d", points[2]);
    strcat(msg, num);
    msg[strlen(msg)] = 'W';
    sprintf(num, "%d", points[3]);
    strcat(msg, num);
    strcat(msg, "\r\n");

    for (int i = 1; i <= NO_PLAYERS; i++) {
        raport(poll_fds[i].fd, msg, true);
        ssize_t written_length = writen(poll_fds[i].fd, msg, strlen(msg));
        if (written_length < 0) {
            syserr("writen");
        }
        else if ((size_t) written_length != strlen(msg)) {
            fatal("incomplete writen");
        }
    }

    for (int i = 0; i < NO_PLAYERS; i++) {
        total_points[i] += points[i];
    }

    memset(msg, 0, BUF_SIZE * sizeof(char));
    strcat(msg, "TOTALN");
    sprintf(num, "%d", total_points[0]);
    strcat(msg, num);
    msg[strlen(msg)] = 'E';
    sprintf(num, "%d", total_points[1]);
    strcat(msg, num);
    msg[strlen(msg)] = 'S';
    sprintf(num, "%d", total_points[2]);
    strcat(msg, num);
    msg[strlen(msg)] = 'W';
    sprintf(num, "%d", total_points[3]);
    strcat(msg, num);
    strcat(msg, "\r\n");

    for (int i = 1; i <= NO_PLAYERS; i++) {
        raport(poll_fds[i].fd, msg, true);
        ssize_t written_length = writen(poll_fds[i].fd, msg, strlen(msg));
        if (written_length < 0) {
            syserr("writen");
        }
        else if ((size_t) written_length != strlen(msg)) {
            fatal("incomplete writen");
        }
    }
    free(msg);
    current_game++;
    if (current_game < no_of_games) {
        send_new_deal();
    }
}



int main(int argc, char *argv[]) {
    parse_args(argc, argv);

    parse_game_file();

    int server_fd = prepare_connection();

    for (int i = 0; i < NO_PLAYERS; i++) {
        total_points[i] = 0;
    }

    // Initialize poll structure.
    for (int i = 0; i < POLL_SIZE; i++) {
        poll_fds[i].fd = -1;
        poll_fds_info[i].fd = -1;
    }
    poll_fds[0].fd = server_fd;
    poll_fds[0].events = POLLIN;
    poll_fds_info[0].fd = server_fd;

    // Main game loop for tracking game progress.
    for (int i = 0; i < no_of_games; i++) {
        get_players();
        play();
    }

    for (int i = 1; i <= NO_PLAYERS; i++) {
        close(poll_fds[i].fd);
    }

    
    free(game_desc);
}