#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <netdb.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "err.h"
#include "common.h"

uint16_t read_port(char const *string) {
    char *endptr;
    errno = 0;
    unsigned long port = strtoul(string, &endptr, 10);
    if (errno != 0 || *endptr != 0 || port > UINT16_MAX) {
        fatal("%s is not a valid port number", string);
    }
    return (uint16_t) port;
}

time_t read_time(char const *string) {
    char *endptr;
    errno = 0;
    time_t number = strtoul(string, &endptr, 10);
    if (errno != 0 || *endptr != 0) {
        fatal("%s is not a valid number", string);
    }
    return number;
}

size_t read_size(char const *string) {
    char *endptr;
    errno = 0;
    unsigned long long number = strtoull(string, &endptr, 10);
    if (errno != 0 || *endptr != 0 || number > SIZE_MAX) {
        fatal("%s is not a valid number", string);
    }
    return number;
}

struct sockaddr_storage get_server_address(char const *host, uint16_t port, int family) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = family; // Unspecified address family.
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *address_result;
    int errcode = getaddrinfo(host, NULL, &hints, &address_result);
    if (errcode != 0) {
        fatal("getaddrinfo: %s", gai_strerror(errcode));
    }

    struct sockaddr_storage send_address;
    memset(&send_address, 0, sizeof(struct sockaddr_storage));

    if (address_result->ai_family == AF_INET) {
        struct sockaddr_in *addr4 = (struct sockaddr_in *)&send_address;
        addr4->sin_family = AF_INET; // IPv4
        addr4->sin_addr.s_addr = 
            ((struct sockaddr_in *)(address_result->ai_addr))->sin_addr.s_addr;
        addr4->sin_port = htons(port);
    } else if (address_result->ai_family == AF_INET6) {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&send_address;
        addr6->sin6_family = AF_INET6; // IPv6
        addr6->sin6_addr = 
            ((struct sockaddr_in6 *)(address_result->ai_addr))->sin6_addr;
        addr6->sin6_port = htons(port);
    } else {
        fatal("Unsupported address family: %d", address_result->ai_family);
    }

    freeaddrinfo(address_result);

    return send_address;
}

struct sockaddr_in get_server_address_ipv4(char const *host, uint16_t port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *address_result;
    int errcode = getaddrinfo(host, NULL, &hints, &address_result);
    if (errcode != 0) {
        fatal("getaddrinfo: %s", gai_strerror(errcode));
    }

    struct sockaddr_in send_address;
    send_address.sin_family = AF_INET;   // IPv4
    send_address.sin_addr.s_addr =       // IP address
            ((struct sockaddr_in *) (address_result->ai_addr))->sin_addr.s_addr;
    send_address.sin_port = htons(port); // port from the command line

    freeaddrinfo(address_result);

    return send_address;
}

struct sockaddr_in6 get_server_address_ipv6(char const *host, uint16_t port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET6; // IPv6
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *address_result;
    int errcode = getaddrinfo(host, NULL, &hints, &address_result);
    if (errcode != 0) {
        fatal("getaddrinfo: %s", gai_strerror(errcode));
    }

    struct sockaddr_in6 server_address;
    server_address.sin6_family = AF_INET6;  // IPv6
    server_address.sin6_addr =
            ((struct sockaddr_in6 *) (address_result->ai_addr))->sin6_addr;
    server_address.sin6_port = htons(port);

    freeaddrinfo(address_result);

    return server_address;
}

char *read_msg(int socket_fd) {
    char *msg = malloc((1 + BUF_SIZE) * sizeof(char));
    memset(msg, 0, (1 + BUF_SIZE) * sizeof(char));
    int ptr = 0;

    ssize_t read_length = readn(socket_fd, msg + ptr, 1);
    ptr++;
    do {
        read_length = readn(socket_fd, msg + ptr, 1);
        if (read_length < 0) {
            syserr("readn");
        }
        else if (read_length == 0) {
            free(msg);
            return NULL;
        }
        ptr += read_length;
    } while (ptr < BUF_SIZE && msg[ptr - 1]!= '\n' && msg[ptr - 2]!= '\r');

    return msg;
}

// Following two functions come from Stevens' "UNIX Network Programming" book.
// Read n bytes from a descriptor. Use in place of read() when fd is a stream socket.
ssize_t readn(int fd, void *vptr, size_t n) {
    ssize_t nleft, nread;
    char *ptr;

    ptr = vptr;
    nleft = n;
    while (nleft > 0) {
        if ((nread = read(fd, ptr, nleft)) < 0)
            return nread;     // When error, return < 0.
        else if (nread == 0)
            break;            // EOF

        nleft -= nread;
        ptr += nread;
    }
    return n - nleft;         // return >= 0
}

// Write n bytes to a descriptor.
ssize_t writen(int fd, const void *vptr, size_t n) {
    ssize_t nleft, nwritten;
    const char *ptr;

    ptr = vptr;               // Can't do pointer arithmetic on void*.
    nleft = n;
    while (nleft > 0) {
        if ((nwritten = write(fd, ptr, nleft)) <= 0)
            return nwritten;  // error

        nleft -= nwritten;
        ptr += nwritten;
    }
    return n;
}

void install_signal_handler(int signal, void (*handler)(int), int flags) {
    struct sigaction action;
    sigset_t block_mask;

    sigemptyset(&block_mask);
    action.sa_handler = handler;
    action.sa_mask = block_mask;
    action.sa_flags = flags;

    if (sigaction(signal, &action, NULL) < 0 ){
        syserr("sigaction");
    }
}
