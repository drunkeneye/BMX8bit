/*
 * rs232_raw_tcp.c - raw TCP transport for RS232 network backend.
 */

#include "vice.h"

#ifdef HAVE_RS232NET

#include <errno.h>

#include "bmc64_log.h"
#include "rs232net_debug.h"
#include "rs232net_internal.h"

void rs232net_closesocket(int index)
{
#ifdef BMC64_DEBUG_PROFILE
    BMC64_RS232_DEBUG("closesocket fd %d had_socket %d deferred %d inuse %d",
                      index, fds[index].fd ? 1 : 0, fds[index].deferred,
                      fds[index].inuse);
#endif
    if (fds[index].fd) {
        vice_network_socket_close(fds[index].fd);
    }
#ifdef RASPI_COMPILE
    rs232net_async_stop(index);
#endif
    fds[index].fd = 0;
    fds[index].deferred = 0;
    fds[index].rx_disconnect_pending = 0;
    fds[index].modem_connect_pending = 0;
    fds[index].rx_flow_paused = 0;
    fds[index].tx_flow_paused = 0;
    fds[index].cts_backpressure = 0;
    fds[index].cts_in = 1;
    fds[index].xonxoff_dte_paused = 0;
    fds[index].xonxoff_xoff_sent = 0;
    rs232net_clear_rxbuf(index);
}

void rs232net_disconnect_deferred(int index, const char *reason)
{
#ifdef BMC64_DEBUG_PROFILE
    BMC64_RS232_EVENT("disconnect fd %d reason %s had_socket %d deferred %d inuse %d",
                      index, reason, fds[index].fd ? 1 : 0,
                      fds[index].deferred, fds[index].inuse);
#endif
    if (fds[index].fd) {
        vice_network_socket_close(fds[index].fd);
    }
#ifdef RASPI_COMPILE
    rs232net_async_stop(index);
#endif
    fds[index].fd = NULL;
    fds[index].deferred = 1;
    fds[index].dcd_in = 0;
    fds[index].ri_in = 0;
    fds[index].rx_disconnect_pending = 0;
    fds[index].modem_connect_pending = 0;
    fds[index].rx_flow_paused = 0;
    fds[index].tx_flow_paused = 0;
    fds[index].cts_backpressure = 0;
    fds[index].cts_in = 1;
    fds[index].xonxoff_dte_paused = 0;
    fds[index].xonxoff_xoff_sent = 0;
    rs232net_clear_rxbuf(index);
}

void rs232net_mark_disconnect_pending(int index, const char *reason)
{
#ifdef BMC64_DEBUG_PROFILE
    BMC64_RS232_EVENT("disconnect pending fd %d reason %s pending %lu/%lu",
                      index, reason,
                      (unsigned long)rs232net_rxbuf_pending(index),
                      (unsigned long)sizeof fds[index].rxbuf);
#endif
    if (fds[index].fd) {
        vice_network_socket_close(fds[index].fd);
    }
#ifdef RASPI_COMPILE
    rs232net_async_stop(index);
#endif
    fds[index].fd = NULL;
    fds[index].deferred = 0;
    fds[index].rx_disconnect_pending = 1;
    fds[index].modem_connect_pending = 0;
    fds[index].xonxoff_dte_paused = 0;
    fds[index].xonxoff_xoff_sent = 0;
    rs232net_flow_update(index, "disconnect-pending");
}

void rs232net_complete_pending_disconnect(int index)
{
    if (!fds[index].rx_disconnect_pending || rs232net_rxbuf_pending(index) > 0) {
        return;
    }

    fds[index].rx_disconnect_pending = 0;
    if (fds[index].hayes) {
        mdm_disconnect(&fds[index].modem, TRUE);
    } else {
        rs232net_closesocket(index);
    }
}

int rs232net_link_active(int index)
{
    return rs232net_socket_active(index) || fds[index].rx_disconnect_pending;
}

int rs232net_ensure_connected(int fd)
{
    if (!rs232net_fd_is_valid(fd)) {
        log_error(rs232net_log, "Attempt to connect invalid fd %d.", fd);
        return -1;
    }
    return rs232net_connect_target(fd, rs232_devfile[fds[fd].device]);
}

int rs232net_connect_target(int fd, const char *target)
{
#ifndef RASPI_COMPILE
    vice_network_socket_address_t *ad = NULL;
#endif
    unsigned long start;

    if (!rs232net_fd_is_valid(fd)) {
        log_error(rs232net_log, "Attempt to connect invalid fd %d.", fd);
        return -1;
    }
    if (!rs232net_fd_is_open(fd)) {
        log_error(rs232net_log, "Attempt to connect non-open fd %d.", fd);
        return -1;
    }
    if (rs232net_socket_active(fd)) {
#ifdef BMC64_DEBUG_PROFILE
        BMC64_RS232_DEBUG("connect skip fd %d already connected", fd);
#endif
        return 0;
    }
    if (rs232net_socket_connecting(fd)) {
#ifdef BMC64_DEBUG_PROFILE
        BMC64_RS232_DEBUG("connect skip fd %d already connecting", fd);
#endif
        return 1;
    }
    if (!fds[fd].deferred) {
#ifdef BMC64_DEBUG_PROFILE
        BMC64_RS232_DEBUG("connect skip fd %d deferred 0 socket 0", fd);
#endif
        return -1;
    }
#ifdef RASPI_COMPILE
    if (!emux_network_is_ready()) {
        BMC64_RS232_EVENT("network not ready; deferring connect fd %d target %s",
                          fd, target ? target : "");
        return -1;
    }
#endif

    start = circle_get_ticks();
    (void)start;
    BMC64_RS232_EVENT("connect fd %d device %d target %s", fd,
                      fds[fd].device, target);

#ifdef RASPI_COMPILE
    if (rs232net_async_start(fd, target) < 0) {
        log_error(rs232net_log, "Cant start async connection.");
        BMC64_RS232_EVENT("connect start failed target %s", target);
        fds[fd].deferred = 1;
        return -1;
    }
    fds[fd].deferred = 1;
    rs232net_clear_rxbuf(fd);
    fds[fd].rx_disconnect_pending = 0;
    rs232net_flow_update(fd, "connect-start");
#ifdef BMC64_DEBUG_PROFILE
    BMC64_RS232_DEBUG("connect pending fd %d device %d target %s after %lu us",
                      fd, fds[fd].device, target, circle_get_ticks() - start);
#endif
    return 1;
#else
    ad = vice_network_address_generate(target, 0);
    if (!ad) {
        log_error(rs232net_log, "Bad device name.  Should be ipaddr:port, but is '%s'.",
                  target);
#ifdef BMC64_DEBUG_PROFILE
        BMC64_RS232_EVENT("connect bad-target fd %d target '%s'", fd,
                          target ? target : "");
#endif
        fds[fd].deferred = 0;
        return -1;
    }

    fds[fd].fd = vice_network_client(ad);
    vice_network_address_close(ad);
    rs232net_clear_rxbuf(fd);
    fds[fd].rx_disconnect_pending = 0;

    if (!fds[fd].fd) {
        log_error(rs232net_log, "Cant open connection.");
        BMC64_RS232_EVENT("connect failed after %lu us target %s err %d",
                          circle_get_ticks() - start, target,
                          vice_network_get_errorcode());
        fds[fd].deferred = 1;
        return -1;
    }

    fds[fd].deferred = 0;
    rs232net_clear_rxbuf(fd);
    fds[fd].rx_disconnect_pending = 0;
    rs232net_flow_update(fd, "connect-ready");
#ifdef BMC64_DEBUG_PROFILE
    rs232net_dbg_reset_fd(fd);
#endif
    BMC64_RS232_EVENT("connect ready after %lu us", circle_get_ticks() - start);
#ifdef BMC64_DEBUG_PROFILE
    BMC64_RS232_DEBUG("connect ready fd %d device %d ip232 %d target %s",
                      fd, fds[fd].device, fds[fd].useip232, target);
#endif
    return 0;
#endif
}

int rs232net_raw_putc(int fd, uint8_t b)
{
    ssize_t n;

    if (!rs232net_fd_is_valid(fd)) {
        log_error(rs232net_log, "Attempt to write to invalid fd %d.", fd);
        return -1;
    }
    if (!rs232net_fd_is_open(fd)) {
        log_error(rs232net_log, "Attempt to write to non-open fd %d.", fd);
        return -1;
    }

    if (!rs232net_socket_active(fd) && !rs232net_socket_connecting(fd) &&
        fds[fd].deferred) {
        if (rs232net_ensure_connected(fd) < 0) {
            return -1;
        }
    }

#ifdef RASPI_COMPILE
    if (fds[fd].async) {
        int written = bmc64_async_net_send(fds[fd].async, &b, 1);
        n = written == 1 ? 1 : -1;
#ifdef BMC64_DEBUG_PROFILE
        rs232net_dbg_tx_count++;
        {
            unsigned long now = circle_get_ticks();
            unsigned long since_rx = fds[fd].dbg_last_rx_fill_clk ?
                now - fds[fd].dbg_last_rx_fill_clk : 0;
            (void)since_rx;
            if (n > 0) {
                fds[fd].dbg_tx_total += (unsigned long)n;
                fds[fd].dbg_last_tx_clk = now;
            }
            if (rs232net_dbg_should_log(rs232net_dbg_tx_count)) {
                BMC64_RS232_TRACE("socket-tx #%u fd %d dev %d byte 0x%02x '%c' "
                                  "queued %ld total_tx %lu since_rx_clk %lu clk %lu",
                                  rs232net_dbg_tx_count, fd, fds[fd].device, b,
                                  rs232net_dbg_chr(b), (long)n,
                                  fds[fd].dbg_tx_total, since_rx, now);
            }
        }
#endif
        if (n < 0) {
            rs232net_flow_note_tx_queue_full(fd);
            log_error(rs232net_log, "Error queueing output.");
#ifdef BMC64_DEBUG_PROFILE
            BMC64_RS232_EVENT("socket-tx-queue-full fd %d", fd);
#endif
            return -1;
        }
        rs232net_flow_update(fd, "raw-tx");
        return 0;
    }
#endif

    if (!fds[fd].fd) {
        return 0;
    }

    DEBUG_LOG_MESSAGE((rs232net_log, "Output 0x%02x.", b));

    n = vice_network_send(fds[fd].fd, &b, 1, 0);
#ifdef BMC64_DEBUG_PROFILE
    rs232net_dbg_tx_count++;
    {
        unsigned long now = circle_get_ticks();
        unsigned long since_rx = fds[fd].dbg_last_rx_fill_clk ?
            now - fds[fd].dbg_last_rx_fill_clk : 0;
        (void)since_rx;
        if (n > 0) {
            fds[fd].dbg_tx_total += (unsigned long)n;
            fds[fd].dbg_last_tx_clk = now;
        }
        if (rs232net_dbg_should_log(rs232net_dbg_tx_count)) {
            BMC64_RS232_TRACE("socket-tx #%u fd %d dev %d byte 0x%02x '%c' "
                              "result %ld total_tx %lu since_rx_clk %lu clk %lu",
                              rs232net_dbg_tx_count, fd, fds[fd].device, b,
                              rs232net_dbg_chr(b), (long)n,
                              fds[fd].dbg_tx_total, since_rx, now);
        }
    }
#endif
    if (n < 0) {
        log_error(rs232net_log, "Error writing: %d.", vice_network_get_errorcode());
#ifdef BMC64_DEBUG_PROFILE
        BMC64_RS232_EVENT("socket-tx-error fd %d err %d", fd,
                          vice_network_get_errorcode());
#endif
        rs232net_closesocket(fd);
        return -1;
    }

    return 0;
}

int rs232net_raw_getc(int fd, uint8_t *b)
{
    int ret;
    ssize_t no_of_read_byte = -1;

    do {
        if (!rs232net_fd_is_valid(fd)) {
            log_error(rs232net_log, "Attempt to read from invalid fd %d.", fd);
            break;
        }

        if (!rs232net_fd_is_open(fd)) {
            log_error(rs232net_log, "Attempt to read from non-open fd %d.", fd);
            break;
        }

        no_of_read_byte = 0;

        if (rs232net_rxbuf_pending(fd) > 0) {
            if (rs232net_socket_active(fd)) {
                rs232net_fill_rxbuf_from_socket(fd, "raw-prefetch");
            }
            *b = fds[fd].rxbuf[fds[fd].rxbuf_pos++];
            rs232net_complete_pending_disconnect(fd);
            rs232net_flow_update(fd, "raw-rx");
#ifdef BMC64_DEBUG_PROFILE
            rs232net_dbg_rx_count++;
            if (rs232net_dbg_should_log(rs232net_dbg_rx_count)) {
                BMC64_RS232_TRACE("socket-rx #%u fd %d dev %d byte 0x%02x '%c' buffered %lu/%lu",
                                  rs232net_dbg_rx_count, fd, fds[fd].device,
                                  *b, rs232net_dbg_chr(*b),
                                  (unsigned long)fds[fd].rxbuf_pos,
                                  (unsigned long)fds[fd].rxbuf_len);
            }
#endif
            no_of_read_byte = 1;
            break;
        }

        if (!rs232net_socket_active(fd)) {
            break;
        }

        rs232net_clear_rxbuf(fd);

        ret = rs232net_fill_rxbuf_from_socket(fd, "raw");
        if (ret < 0) {
            no_of_read_byte = -1;
        } else if (rs232net_rxbuf_pending(fd) > 0) {
            *b = fds[fd].rxbuf[fds[fd].rxbuf_pos++];
            rs232net_complete_pending_disconnect(fd);
            rs232net_flow_update(fd, "raw-rx");
            DEBUG_LOG_MESSAGE((rs232net_log, "Input 0x%02x.", *b));
#ifdef BMC64_DEBUG_PROFILE
            rs232net_dbg_rx_count++;
            if (rs232net_dbg_should_log(rs232net_dbg_rx_count)) {
                BMC64_RS232_TRACE("socket-rx #%u fd %d dev %d byte 0x%02x '%c' buffered %lu/%lu",
                                  rs232net_dbg_rx_count, fd, fds[fd].device,
                                  *b, rs232net_dbg_chr(*b),
                                  (unsigned long)fds[fd].rxbuf_pos,
                                  (unsigned long)fds[fd].rxbuf_len);
            }
#endif
            no_of_read_byte = 1;
        }
    } while (0);

    return (int)no_of_read_byte;
}

#endif
