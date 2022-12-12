#ifndef NETWORK_POSIX_H
#define NETWORK_POSIX_H

#include <sys/socket.h>
#include <arpa/inet.h>

#include "port.h"

typedef struct {
    int verbose;
    int debug;
    int timeout;
    int socket_dat;
    int socket_cmd;
    struct sockaddr_in servaddr_dat;
    struct sockaddr_in servaddr_cmd;
} network_interface_t;

typedef serial_gpio_t network_gpio_t;

#endif
