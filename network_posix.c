#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>
#include <sys/select.h>
#include <pthread.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>

#include "port.h"
#include "network_posix.h"

static network_interface_t *network_open(unsigned int port)
{
    network_interface_t *xface = calloc(2, sizeof (network_interface_t));
    memset(xface, 0, sizeof (network_interface_t));

    struct sockaddr_in *addr;

    if ((xface->socket_dat = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    addr = &xface->servaddr_dat;
    memset(addr, 0, sizeof(struct sockaddr_in));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
    addr->sin_addr.s_addr = INADDR_ANY;

    if ((xface->socket_cmd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    addr = &xface->servaddr_cmd;
    memset(addr, 0, sizeof(struct sockaddr_in));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port+1);
    addr->sin_addr.s_addr = INADDR_ANY;

    return xface;
}

static int network_flush(network_interface_t *xface)
{
    return PORT_ERR_OK;
}

static int network_close(network_interface_t *xface)
{
    close(xface->socket_dat);
    return PORT_ERR_OK;
}

static port_err_t network_posix_open(struct port_interface *port, struct port_options *ops)
{
    network_interface_t *xface = network_open(ops->port_id);
    port->private = xface;
    return (xface != NULL) ? PORT_ERR_OK : PORT_ERR_UNKNOWN;
}

static port_err_t network_posix_close(struct port_interface *port)
{
    network_interface_t *xface = (network_interface_t *) port->private;
    network_close(xface);
    port->private = NULL;
    return PORT_ERR_OK;
}

/**
 * @brief check for incoming data from socket, or to transmit outgoing data.
 * @param socket_fd -- socket descriptor for udp port
 * @return 1 if data is ready on the socket; 0 otherwise
 */
unsigned int check_socket(int socket_fd)
{
    /* prepare socket operation timeout */
    struct timeval socket_timeout;
    unsigned long long micros = 100000;
    socket_timeout.tv_sec = 0; /* number of seconds */
    socket_timeout.tv_usec = micros;

    /** get maximum socket fd and populate rset, wset for use by select() */
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(socket_fd, &rset);
    int status = select(socket_fd + 1, &rset, NULL, NULL, &socket_timeout);
    if (status < 0) { return -1; }

    return (FD_ISSET(socket_fd, &rset)) ? 1 : 0;
}

#define RX_DATA_BUFF_LENGTH (4096)

static uint8_t rx_data_buff[RX_DATA_BUFF_LENGTH];
static unsigned int rx_data_head = 0;
static unsigned int rx_data_tail = 0;
static unsigned int rx_data_mask = RX_DATA_BUFF_LENGTH - 1;

static void drain_udp_socket(network_interface_t *xface)
{
    int status = check_socket(xface->socket_dat);
    if (status > 0) {
        static uint8_t tmp_rx_buff[512];
        socklen_t len = sizeof (xface->servaddr_dat);
        ssize_t n = recvfrom(xface->socket_dat, tmp_rx_buff, sizeof (tmp_rx_buff), 0, (struct sockaddr *) &xface->servaddr_dat, &len);
        for (int i = 0; i < n; ++i) { /* unconditionally wrap rx buffer if not serviced. no checking for wraps */
            rx_data_buff[rx_data_head] = tmp_rx_buff[i];
            rx_data_head = (rx_data_head + 1) & rx_data_mask;
        }
    }
}

static port_err_t network_posix_read(struct port_interface *port, void *buf, size_t n_bytes)
{
    network_interface_t *xface = (network_interface_t *) port->private;
    uint8_t *data = (uint8_t *) buf;
    ssize_t index = 0;
    while (index < n_bytes) { /* TODO timeout operation */
        drain_udp_socket(port->private);
        while (rx_data_tail != rx_data_head) {
            uint8_t byte = rx_data_buff[rx_data_tail];
            data[index++] = byte;
            fprintf(stderr, "net-rx %d/%d-byte = %2.2x\n", index, n_bytes, byte);
            rx_data_tail = (rx_data_tail + 1) & rx_data_mask;
            if (index == n_bytes) {
                break;
            }
        }
    }

    return PORT_ERR_OK;
}

static port_err_t network_posix_write(struct port_interface *port, void *buf, size_t n_bytes)
{
    network_interface_t *xface = (network_interface_t *) port->private;
    ssize_t n;

    const uint8_t *pos = (const uint8_t *) buf;

    while (n_bytes) { /* TODO timeout operation */
        n = sendto(xface->socket_dat, pos, n_bytes, 0, (const struct sockaddr *) &xface->servaddr_dat, sizeof(xface->servaddr_dat));
        for (int i = 0; i < n; ++i) { fprintf(stderr, "net-tx %d-byte = %2.2x\n", i, pos[i]); } /* TODO verbose option */
        if (n < 1)  { return PORT_ERR_UNKNOWN; }
        n_bytes -= n;
        pos += n;
    }
    return PORT_ERR_OK;
}

static port_err_t network_posix_gpio(struct port_interface *port, network_gpio_t bit, int level)
{
    network_interface_t *xface = (network_interface_t *) port->private;
    char obuf[32];

    switch (bit) {
        case GPIO_RTS: {
            ssize_t olen = snprintf(obuf, sizeof(obuf), "$RTS,%d*", level);
            sendto(xface->socket_cmd, obuf, olen, 0, (const struct sockaddr *) &xface->servaddr_cmd, sizeof(xface->servaddr_cmd));
            break;
        }

        case GPIO_DTR: {
            ssize_t olen = snprintf(obuf, sizeof(obuf), "$DTR,%d*", level);
            sendto(xface->socket_cmd, obuf, olen, 0, (const struct sockaddr *) &xface->servaddr_cmd,
                   sizeof(xface->servaddr_cmd));
            break;
        }

        default: {
            return PORT_ERR_UNKNOWN;
        }
    }

    return PORT_ERR_OK;
}

static const char *network_posix_get_cfg_str(struct port_interface *port)
{
    return "";
}

static port_err_t network_posix_flush(struct port_interface *port)
{
    network_interface_t *xface = (network_interface_t *) port->private;
    network_flush(xface);
    return PORT_ERR_OK;
}

struct port_interface port_network = {
        .name	= "network_posix",
        .flags	= PORT_BYTE | PORT_GVR_ETX | PORT_CMD_INIT | PORT_RETRY,
        .open	= network_posix_open,
        .close	= network_posix_close,
        .flush  = network_posix_flush,
        .read	= network_posix_read,
        .write	= network_posix_write,
        .gpio	= network_posix_gpio,
        .get_cfg_str = network_posix_get_cfg_str,
};
