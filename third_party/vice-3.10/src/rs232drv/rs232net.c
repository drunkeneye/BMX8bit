/*
 * rs232net.c - RS232 over network emulation.
 *
 * This file keeps the public VICE RS232 network backend entry points.  The
 * transport, Hayes modem, status-line, RX-buffer, async, and debug helpers live
 * in the rs232_* modules next to it.
 */

#undef DEBUG
/* #define DEBUG */

#include "vice.h"

#ifdef HAVE_RS232NET

#include <string.h>

#include "bmc64_log.h"
#include "log.h"
#include "rs232net.h"
#include "rs232net_debug.h"
#include "rs232net_internal.h"
#include "tcpser/modem_core.h"
#include "tcpser/phone_book.h"
#include "vicesocket.h"

rs232net_t fds[RS232_NUM_DEVICES];
log_t rs232net_log = LOG_DEFAULT;
int rs232net_hayes_mode;
int rs232net_hayes_audio_mode;
static char rs232net_phonebook_path[256];

int rs232net_resources_init(void)
{
    return 0;
}

void rs232net_resources_shutdown(void)
{
}

int rs232net_cmdline_options_init(void)
{
    return 0;
}

void rs232net_set_hayes_mode(int enabled)
{
    rs232net_hayes_mode = enabled ? 1 : 0;
    BMC64_RS232_EVENT("mode %s", rs232net_hayes_mode ? "hayes" : "raw");
}

int rs232net_get_hayes_mode(void)
{
    return rs232net_hayes_mode;
}

void rs232net_set_hayes_audio_mode(int mode)
{
    if (mode < 0 || mode > RS232NET_HAYES_AUDIO_LONG) {
        mode = 0;
    }
    rs232net_hayes_audio_mode = mode;
#ifdef RASPI_COMPILE
    bmc64_modem_audio_set_mode(mode);
#endif
}

int rs232net_load_phonebook(const char *path)
{
    int rc;

    if (path != NULL && path[0] != '\0') {
        strncpy(rs232net_phonebook_path, path,
                sizeof(rs232net_phonebook_path) - 1);
        rs232net_phonebook_path[sizeof(rs232net_phonebook_path) - 1] = '\0';
    } else {
        rs232net_phonebook_path[0] = '\0';
    }

    rc = pb_load_file(rs232net_phonebook_path);
    BMC64_RS232_EVENT("phonebook %s %s",
                      rs232net_phonebook_path[0] ? rs232net_phonebook_path : "none",
                      rc == 0 ? "loaded" : "failed");
    return rc;
}

int rs232net_fd_is_open(int fd)
{
    return rs232net_fd_is_valid(fd) && fds[fd].inuse;
}

int rs232net_fd_is_valid(int fd)
{
    return fd >= 0 && fd < RS232_NUM_DEVICES;
}

int rs232net_fd_is_hayes(int fd)
{
    return rs232net_fd_is_open(fd) && fds[fd].hayes;
}

void rs232net_init(void)
{
    rs232net_log = log_open("RS232NET");
    mdm_init();
    pb_init();
    if (rs232net_phonebook_path[0] != '\0') {
        rs232net_load_phonebook(rs232net_phonebook_path);
    }
}

void rs232net_reset(void)
{
    int i;

    for (i = 0; i < RS232_NUM_DEVICES; i++) {
        if (fds[i].inuse) {
            rs232net_close(i);
        }
    }
}

int rs232net_open(int device)
{
    vice_network_socket_address_t *ad = NULL;
    int index = -1;
#ifdef BMC64_DEBUG_PROFILE
    unsigned long start = circle_get_ticks();
#endif

    BMC64_RS232_EVENT("open device %d target %s deferred", device,
                      rs232_devfile[device]);

    do {
        int i;

#ifndef RASPI_COMPILE
        if (!rs232net_hayes_mode) {
            ad = vice_network_address_generate(rs232_devfile[device], 0);
            if (!ad) {
                log_error(rs232net_log, "Bad device name.  Should be ipaddr:port, but is '%s'.", rs232_devfile[device]);
                break;
            }
        }
#endif

        for (i = 0; i < RS232_NUM_DEVICES; i++) {
            if (!fds[i].inuse) {
                break;
            }
        }

        if (i >= RS232_NUM_DEVICES) {
            log_error(rs232net_log, "No more devices available.");
            break;
        }

        DEBUG_LOG_MESSAGE((rs232net_log, "rs232net_open(device=%d).", device));

        fds[i].inuse = 1;
        fds[i].fd = NULL;
        fds[i].device = device;
        fds[i].deferred = 1;
        fds[i].useip232 = rs232net_hayes_mode ? 0 : rs232_useip232[device];
        fds[i].dcd_in = 0;
        fds[i].ri_in = 0;
        fds[i].dtr_out = 0;
        rs232net_flow_reset(i);
        fds[i].hayes = rs232net_hayes_mode;
        fds[i].rx_disconnect_pending = 0;
        fds[i].modem_connect_pending = 0;
#ifdef RASPI_COMPILE
        fds[i].async = NULL;
#endif
        rs232net_clear_rxbuf(i);
#ifdef BMC64_DEBUG_PROFILE
        rs232net_dbg_reset_fd(i);
#endif
        if (fds[i].hayes) {
            rs232net_hayes_init_fd(i);
        }

        index = i;
#ifdef BMC64_DEBUG_PROFILE
        BMC64_RS232_DEBUG("open logical_fd %d device %d target %s ip232 %d mode %s deferred 1",
                          index, device, rs232_devfile[device],
                          fds[i].useip232, fds[i].hayes ? "hayes" : "raw");
#endif
        if (!fds[i].hayes) {
            if (rs232net_ensure_connected(i) < 0) {
                BMC64_RS232_EVENT("open raw connect failed, will retry on output");
            }
        }
    } while (0);

    if (ad) {
        vice_network_address_close(ad);
    }

#ifdef BMC64_DEBUG_PROFILE
    BMC64_RS232_EVENT("open deferred %s after %lu us",
                      index >= 0 ? "ready" : "failed",
                      circle_get_ticks() - start);
#endif

    return index;
}

void rs232net_reopen_device(int device)
{
    int i;

    for (i = 0; i < RS232_NUM_DEVICES; i++) {
        if (fds[i].inuse && fds[i].device == device) {
            if (fds[i].fd) {
                vice_network_socket_close(fds[i].fd);
            }
#ifdef RASPI_COMPILE
            rs232net_async_stop(i);
#endif
            fds[i].fd = NULL;
            fds[i].deferred = 1;
            fds[i].useip232 = rs232net_hayes_mode ? 0 : rs232_useip232[device];
            fds[i].dcd_in = 0;
            fds[i].ri_in = 0;
            fds[i].dtr_out = 0;
            rs232net_flow_reset(i);
            fds[i].hayes = rs232net_hayes_mode;
            fds[i].rx_disconnect_pending = 0;
            fds[i].modem_connect_pending = 0;
            rs232net_clear_rxbuf(i);
#ifdef BMC64_DEBUG_PROFILE
            rs232net_dbg_reset_fd(i);
#endif
            if (fds[i].hayes) {
                rs232net_hayes_init_fd(i);
            }
            BMC64_RS232_EVENT("reopen logical_fd %d device %d target %s ip232 %d mode %s",
                              i, device, rs232_devfile[device],
                              fds[i].useip232,
                              fds[i].hayes ? "hayes" : "raw");
        }
    }
}

void rs232net_close(int fd)
{
    do {
        DEBUG_LOG_MESSAGE((rs232net_log, "close(fd=%d).", fd));

        if (!rs232net_fd_is_valid(fd)) {
            log_error(rs232net_log, "Attempt to close invalid fd %d.", fd);
            break;
        }
        if (!rs232net_fd_is_open(fd)) {
            log_error(rs232net_log, "Attempt to close non-open fd %d.", fd);
            break;
        }

        if (fds[fd].useip232 && fds[fd].fd) {
            rs232net_raw_putc(fd, IP232MAGIC);
            rs232net_raw_putc(fd, IP232DTRLO);
        }

#ifdef BMC64_DEBUG_PROFILE
        BMC64_RS232_DEBUG("close fd %d device %d ip232 %d socket %d",
                          fd, fds[fd].device, fds[fd].useip232,
                          fds[fd].fd ? 1 : 0);
#endif
        rs232net_closesocket(fd);
        fds[fd].inuse = 0;
    } while (0);
}

int rs232net_putc(int fd, uint8_t b)
{
    if (!rs232net_fd_is_valid(fd)) {
        log_error(rs232net_log, "Attempt to write to invalid fd %d.", fd);
        return -1;
    }
    if (!rs232net_fd_is_open(fd)) {
        log_error(rs232net_log, "Attempt to write to non-open fd %d.", fd);
        return -1;
    }

    if (rs232net_fd_is_hayes(fd)) {
        return rs232net_hayes_putc(fd, b);
    }

    if (fds[fd].useip232 && b == IP232MAGIC) {
        if (rs232net_raw_putc(fd, IP232MAGIC) == -1) {
            return -1;
        }
    }

    return rs232net_raw_putc(fd, b);
}

int rs232net_getc(int fd, uint8_t *b)
{
    int ret = -1;

    if (!rs232net_fd_is_valid(fd)) {
        log_error(rs232net_log, "Attempt to read from invalid fd %d.", fd);
        return -1;
    }

    if (rs232net_fd_is_hayes(fd)) {
        return rs232net_hayes_getc(fd, b);
    }

tryagain:
    ret = rs232net_raw_getc(fd, b);
    if (ret <= 0) {
        return ret;
    }

    if (fds[fd].useip232 && *b == IP232MAGIC) {
        if ((ret = rs232net_raw_getc(fd, b)) < 1) {
            return ret;
        }
        if (*b != IP232MAGIC) {
            fds[fd].dcd_in = (*b & IP232DCDMASK) == IP232DCDHI ? 1 : 0;
            fds[fd].ri_in = (*b & IP232RIMASK) == IP232RIHI ? 1 : 0;
#ifdef BMC64_DEBUG_PROFILE
            BMC64_RS232_DEBUG("ip232-status fd %d raw 0x%02x dcd %d ri %d",
                              fd, *b, fds[fd].dcd_in, fds[fd].ri_in);
#endif
            goto tryagain;
        }
    }

    return ret;
}

#endif
