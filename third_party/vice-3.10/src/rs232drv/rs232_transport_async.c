/*
 * rs232_transport_async.c - BMC64 async network transport glue.
 */

#include "vice.h"

#ifdef HAVE_RS232NET

#include "bmc64_log.h"
#include "rs232net_debug.h"
#include "rs232net_internal.h"

void rs232net_async_stop(int fd)
{
#ifdef RASPI_COMPILE
    if (fd < 0 || fd >= RS232_NUM_DEVICES || !fds[fd].async) {
        return;
    }
    bmc64_async_net_stop(fds[fd].async);
    fds[fd].async = NULL;
#else
    (void)fd;
#endif
}

int rs232net_async_start(int fd, const char *target)
{
#ifdef RASPI_COMPILE
    if (fd < 0 || fd >= RS232_NUM_DEVICES || !target || target[0] == '\0') {
        return -1;
    }

    rs232net_async_stop(fd);
    fds[fd].async = bmc64_async_net_start(target);
    if (!fds[fd].async) {
        return -1;
    }
    return 0;
#else
    (void)fd;
    (void)target;
    return -1;
#endif
}

int rs232net_async_status(int fd)
{
#ifdef RASPI_COMPILE
    if (fd < 0 || fd >= RS232_NUM_DEVICES || !fds[fd].async) {
        return BMC64_ASYNC_NET_CLOSED;
    }
    return bmc64_async_net_status(fds[fd].async);
#else
    (void)fd;
    return BMC64_ASYNC_NET_CLOSED;
#endif
}

int rs232net_socket_active(int index)
{
#ifdef RASPI_COMPILE
    return fds[index].async &&
           rs232net_async_status(index) == BMC64_ASYNC_NET_CONNECTED;
#else
    return fds[index].fd != NULL;
#endif
}

int rs232net_socket_connecting(int index)
{
#ifdef RASPI_COMPILE
    return fds[index].async &&
           rs232net_async_status(index) == BMC64_ASYNC_NET_CONNECTING;
#else
    (void)index;
    return 0;
#endif
}

void rs232net_poll_async(int fd)
{
#ifdef RASPI_COMPILE
    int status;

    if (fd < 0 || fd >= RS232_NUM_DEVICES || !fds[fd].async) {
        return;
    }

    status = rs232net_async_status(fd);
    if (fds[fd].hayes && fds[fd].modem_connect_pending) {
        if (status == BMC64_ASYNC_NET_CONNECTED) {
            rs232net_modem_connect_succeeded(fd);
        } else if (status == BMC64_ASYNC_NET_ERROR ||
                   status == BMC64_ASYNC_NET_CLOSED) {
            rs232net_modem_connect_failed(fd);
            rs232net_async_stop(fd);
        }
    }
#else
    (void)fd;
#endif
}

#endif
