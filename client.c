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
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <poll.h>
#include <termios.h>

#define IAM_LEN 6
#define TRICK_LEN 10
#define NO_CARDS 13
#define NO_PLAYERS 4

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
card_t cards[NO_CARDS];

// Helping data for communication with user.
int message_num;
card_t tricks[NO_CARDS + 6][NO_PLAYERS];
card_t card_played;
bool is_finished = false;

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

// Function to print raport about exchanged messages.
static void raport(int socket_fd, char *msg, bool from_server) {
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
    if (from_server) {
        printf("[%s:%d,%s:%d,%s.%03ld] %s\\r\\n\n", server_ip, server_port, 
            local_ip, local_port, time_str, tv.tv_usec / 1000, msg);
    } else {
        printf("[%s:%d,%s:%d,%s.%03ld] %s\\r\\n\n", local_ip, local_port, 
            server_ip, server_port, time_str, tv.tv_usec / 1000, msg);
    }
    msg[strlen(msg)] = '\r';
}

// Function to print received message to the user.
static void user_interface(char *msg) {
    // Print BUSY message.
    if (strncmp(msg, "BUSY", 4) == 0) {
        char places[3];
        int place = 0;
        for (int i = strlen("BUSY"); msg[i] != '\r'; i++) {
            places[place++] = msg[i];
        }
        printf("%s", msg);
        printf("Place busy, list of busy places received: ");
        for (int i = 0; i < place - 1; i++) {
            printf("%c, ", places[i]);
        }
        printf("%c.\n", places[place - 1]);
    }
    // Print DEAL message.
    else if (strncmp(msg, "DEAL", 4) == 0) {
        printf("%s", msg);
        printf("New deal %c: staring place %c, your cards: ", game_side, starting_player);
        for(int i = 0; i < NO_CARDS - 1; i++) {
            printf("%c", cards[i].num);
            if (cards[i].num == '1') {
                printf("0");
            }
            printf("%c, ", cards[i].col);
        }
        printf("%c", cards[NO_CARDS - 1].num);
        if (cards[NO_CARDS - 1].num == '1') {
            printf("0");
        }
        printf("%c.\n", cards[NO_CARDS - 1].col);
    }
    // Print WRONG message.
    else if (strncmp(msg, "WRONG", 5) == 0) {
        printf("%s", msg);
        int ptr = strlen("WRONG");
        printf("Wrong message received in trick ");
        while (msg[ptr]!= '\r') {
            printf("%c", msg[ptr++]);
        }
        printf(".\n");
    }
    // Print TAKEN message.
    else if (strncmp(msg, "TAKEN", 5) == 0) {
        printf("%s", msg);
        int ptr = strlen("TAKEN") + (message_num >= 9 ? 2 : 1);
        printf("A trick %d is taken by %c, cards ", message_num + 1, 
            msg[strlen(msg) - 1 - strlen("\r\n")]);
        while (msg[ptr]!= '\r') {
            printf("%c", msg[ptr]);
            if(msg[ptr] == 'C' || msg[ptr] == 'D' || 
               msg[ptr] == 'H' || msg[ptr] == 'S') {
                if (msg[ptr + 1] != '\r') {
                    printf(", ");
                }
            }
            ptr++;
        }
        printf(".\n");
    }
    // Print SCORE message.
    else if (strncmp(msg, "SCORE", 5) == 0) {
        printf("%s", msg);
        printf("The scores are:\n");
        int ptr = strlen("SCORE");
        for (int i = 0; i < NO_PLAYERS; i++) {
            printf("%c | ", msg[ptr++]);
            while (msg[ptr] >= '0' && msg[ptr] <= '9') {
                printf("%c", msg[ptr++]);
            }
            printf("\n");
        }
    }
    // Print TOTAL message.
    else if (strncmp(msg, "TOTAL", 5) == 0) {
        printf("%s", msg);
        printf("The total scores are:\n");
        int ptr = strlen("SCORE");
        for (int i = 0; i < NO_PLAYERS; i++) {
            printf("%c | ", msg[ptr++]);
            while (msg[ptr] >= '0' && msg[ptr] <= '9') {
                printf("%c", msg[ptr++]);
            }
            printf("\n");
        }
    }
    // Print TRICK message.
    else {
        printf("%ld %s", strlen(msg), msg);
        printf("Trick: (%d) ", message_num + 1);
        int ptr = strlen("TRICK") + (message_num >= 9 ? 2 : 1);
        while (msg[ptr]!= '\r') {
            printf("%c", msg[ptr]);
            if(msg[ptr] == 'C' || msg[ptr] == 'D' || 
               msg[ptr] == 'H' || msg[ptr] == 'S') {
                if (msg[ptr + 1] != '\r') {
                    printf(", ");
                }
            }
            ptr++;
        }
        printf("\nAvailable: ");
        for (int i = 0; i < NO_CARDS; i++) {
            if (cards[i].num == 0) {
                continue;
            } else {
                printf("%c", cards[i].num);
                if (cards[i].num == '1') {
                    printf("0");
                }
                printf("%c", cards[i].col);
                if (i!= NO_CARDS - 1) {
                    printf(", ");
                }
            }
        }
        printf("\n");
    }
}

// Function to get game data from the server.
static int get_game_info(int socket_fd) {
    // Receiving DEAL message.
    char *msg = read_msg(socket_fd);
    int msg_len = strlen(msg);
    
    if (is_automatic) {
        raport(socket_fd, msg, true);
    }

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
            for (int i = 0; i < NO_CARDS; i++) {
                cards[i].num = msg[++ptr];
                if (msg[ptr] == '1') {
                    ptr++;
                }
                cards[i].col = msg[++ptr];
            }

            if(!is_automatic) {
                user_interface(msg);
            }
            free(msg);
            return 0;
        } else {
            free(msg);
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

    if(is_automatic) {
        raport(socket_fd, msg, false);
    }

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
    for (int t = 1; t <= NO_CARDS; t++) {
        // Read info about the current trick.
        correct_packet = false;
        while (!correct_packet) {
            msg = read_msg(socket_fd);
            msg_len = strlen(msg);

            if (is_automatic) {
                raport(socket_fd, msg, true);
            }

            if (msg_len < 2 || msg[msg_len - 2] != '\r' || 
                msg[msg_len - 1] != '\n') {
                free(msg);
            } else if (strncmp(msg, "TRICK", 5) == 0) {
                int trick_numer = msg[strlen("TRICK")] - '0';
                if (t >= 10) {
                    trick_numer *= 10;
                    trick_numer += msg[strlen("TRICK") + 1] - '0';
                }
                if (trick_numer != t) {
                    free(msg);
                } else {
                correct_packet = true;
                }
            } else {
                free(msg);
            }
        }

        // Pick a card to play. Strategy is to always play the lowest if
        // possible to win and the highest otherwise.
        
        card_t laid_cards[3]; // Max number of cards in a trick.
        // Fill info about the cards in trick.
        int ptr = 5; // Msg after "TRICK".
        char trick_char = msg[ptr++];
        char trick_second_char;
        if (t >= 10) {
            trick_second_char = msg[ptr++];
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
        card_to_play.num = 0;
        card_to_play.col = 0;
        int card_id = -1;
        if (card == 0) { // No cards in the trick. We pick lowest possible card.
            for(int i = 0; i < NO_CARDS; i++) {
                if (cards[i].num == 0) {
                    continue;
                } else if (card_to_play.num == 0) {
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
            for (int i = 0; i < NO_CARDS; i++) {
                if (cards[i].num == 0) {
                    continue;
                } else if (cards[i].col == highest_card.col && 
                    numtoi(cards[i].num) < numtoi(highest_card.num)) {
                    card_to_play = cards[i];
                    card_id = i;
                }
            }
            if (card_id == -1) {
                // If we can't play lower card, we play the highest card.
                for (int i = 0; i < NO_CARDS; i++) {
                    if (cards[i].num == 0) {
                        continue;
                    } else if (card_to_play.num == 0) {
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
        msg[strlen(msg)] = trick_char;
        if (t >= 10) {
            msg[strlen(msg)] = trick_second_char;
        }
        msg[strlen(msg)] = card_to_play.num;
        if (card_to_play.num == '1') {
            msg[strlen(msg)] = '0';
        }
        msg[strlen(msg)] = card_to_play.col;
        strcat(msg, "\r\n");

        if (is_automatic) {
            raport(socket_fd, msg, false);
        }

        ssize_t written_length = writen(socket_fd, msg, msg_len);
        if (written_length < 0) {
            syserr("writen");
        }
        else if ((size_t) written_length != msg_len) {
            fatal("incomplete writen");
        }
        free(msg);

        cards[card_id].num = 0;
        cards[card_id].col = 0;

        // Receive info about who took the trick.
        correct_packet = false;
        while (!correct_packet) {
            msg = read_msg(socket_fd);
            msg_len = strlen(msg);

            if (is_automatic) {
                raport(socket_fd, msg, true);
            }

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

    // Receive info about who won the hand.
    correct_packet = false;
    while (!correct_packet) {
        msg = read_msg(socket_fd);
        msg_len = strlen(msg);

        if (is_automatic) {
           raport(socket_fd, msg, true);
        }

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

        if (is_automatic) {
           raport(socket_fd, msg, true);
        }

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

static void handle_user_input(int socket_fd, char *msg) {
    if (strcmp(msg, "cards\n") == 0) {
        bool first = true;
        for (int i = 0; i < NO_CARDS; i++) {
            if (cards[i].num == 0) {
                continue;
            }
            if (!first) {
                printf(", ");
            }
            first = false;
            printf("%c", cards[i].num);
            if (cards[i].num == '1') {
                printf("0");
            }
            printf("%c", cards[i].col);
        }
        printf("\n");
    } else if (strcmp(msg, "tricks\n") == 0) {
        for (int i = 0; i < message_num; i++) {
            if (tricks[i][0].num == 0) {
                break;
            }
            for (int j = 0; j < NO_PLAYERS; j++) {
                printf("%c", tricks[i][j].num);
                if (tricks[i][j].num == '1') {
                    printf("0");
                }
                printf("%c", tricks[i][j].col);
                if (j < NO_PLAYERS - 1) {
                    printf(", ");
                }
            }
            printf("\n");
        }
    } else if (strncmp(msg, "!", 1) == 0) {
        // Parse the card.
        card_t card_to_play;
        size_t ptr = 1;
        card_to_play.num = msg[ptr++];
        if (msg[ptr] == '0') {
            ptr++;
        }
        card_to_play.col = msg[ptr++];
        if (msg[ptr] != '\n' || ptr != strlen(msg) - 1) {
            printf("Wrong card format\n");
            return;
        }
        card_played.num = card_to_play.num;
        card_played.col = card_to_play.col;

        // Create the message.
        char *to_send = malloc(BUF_SIZE * sizeof(char));
        memset(to_send, 0, BUF_SIZE * sizeof(char));
        strcpy(to_send, "TRICK");
        to_send[strlen(to_send)] = card_to_play.num;
        if (card_to_play.num == '1') {
            to_send[strlen(to_send)] = '0';
        }
        to_send[strlen(to_send)] = card_to_play.col;
        strcat(to_send, "\r\n");

        // Send the message.
        ssize_t written_length = writen(socket_fd, to_send, strlen(to_send));
        if (written_length < 0) {
            syserr("writen");
        }
        else if ((size_t) written_length != strlen(to_send)) {
            fatal("incomplete writen");
        }
        free(to_send);
    } else {
        printf("Unknown command\n");
    }
}

static void handle_server_input(char *msg) {
    if (message_num <= 12) {
        // We are playing out one of the tricks.
        // We are expecting a TRICK, WRONG, or TAKEN message.
        if (strncmp(msg, "TRICK", 5) == 0) {
            user_interface(msg);
        } else if (strncmp(msg, "WRONG", 5) == 0) {
            user_interface(msg);
            card_played.num = 0;
            card_played.col = 0;
        } else if (strncmp(msg, "TAKEN", 5) == 0) {
            user_interface(msg);

            // Update tricks.
            size_t ptr = strlen("TAKEN") + (message_num >= 9 ? 2 : 1);
            for (int i = 0; i < NO_PLAYERS; i++) {
                tricks[message_num][i].num = msg[ptr++];
                if (msg[ptr] == '0') {
                    ptr++;
                }
                tricks[message_num][i].col = msg[ptr++];
            }

            // Update cards.
            for (int i = 0; i < NO_CARDS; i++) {
                if (cards[i].num == card_played.num && cards[i].col == card_played.col) {
                    cards[i].num = 0;
                    cards[i].col = 0;
                    card_played.num = 0;
                    card_played.col = 0;
                    break;
                }
            }
            message_num++;
        }
    } else if (message_num == 13) {
        // We are expecting a SCORE message.
        if (strncmp(msg, "SCORE", 5) == 0) {
            user_interface(msg);
            message_num++;
        }
    } else if (message_num == 14) {
        // We are expecting a TOTAL message.
        if (strncmp(msg, "TOTAL", 5) == 0) {
            user_interface(msg);
            is_finished = true;
        }
    }
}


static void manual_play(int socket_fd) {
    // Initialize game data.
    message_num = 0;
    for (int i = 0; i < NO_CARDS + 6; i++) {
        for (int j = 0; j < NO_PLAYERS; j++) {
            tricks[i][j].num = 0;
            tricks[i][j].col = 0;
        }
    }
    is_finished = false;
    // Setting up poll
    struct pollfd fds[2];

    // Monitor socket for input
    fds[0].fd = socket_fd;
    fds[0].events = POLLIN;

    // Monitor stdin for input
    fds[1].fd = fileno(stdin);
    fds[1].events = POLLIN;

    char buffer[BUF_SIZE];

    while (!is_finished) {
        int ret = poll(fds, 2, -1); // Wait indefinitely for an event
        if (ret < 0) {
            syserr("poll");
        }

        // Check for server input
        if (fds[0].revents & POLLIN) {
            char *msg = read_msg(socket_fd);
            handle_server_input(msg);
            free(msg);
        }

        // Check for console input
        if (fds[1].revents & POLLIN) {
            memset(buffer, 0, BUF_SIZE);
            if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
                handle_user_input(socket_fd, buffer);
            } else {
                // End of input or error
                if (feof(stdin)) {
                    printf("End of console input.\n");
                    break;
                }
                if (ferror(stdin)) {
                    syserr("fgets");
                }
            }
        }
    }
}


int main(int argc, char *argv[]) {
    parse_args(argc, argv);
    
    int socket_fd = prepare_connection();

    handshake(socket_fd);

    while (true) {
        while (true) {
            if (is_automatic) {
                auto_play(socket_fd);
            } else {
                manual_play(socket_fd);
            }

            // Check if there is more data available.
            char *temp = malloc(sizeof(char) * 2);
            ssize_t read_length = recv(socket_fd, temp, 1, MSG_PEEK);
            if (read_length < 0) {
                syserr("recv");
            } else if (read_length == 0) {
                free(temp);
                close(socket_fd);
                exit(0);
            }
            free(temp);

            get_game_info(socket_fd);
        }
    }
}