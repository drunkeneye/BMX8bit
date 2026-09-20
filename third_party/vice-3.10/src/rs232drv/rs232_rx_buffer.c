/*
 * rs232_rx_buffer.c - shared RS232 network RX buffering.
 */

#include "vice.h"

#ifdef HAVE_RS232NET

#include <string.h>

#include "bmc64_log.h"
#include "rs232net_debug.h"
#include "rs232net_internal.h"

void rs232net_clear_rxbuf(int fd)
{
    fds[fd].rxbuf_pos = 0;
    fds[fd].rxbuf_len = 0;
}

size_t rs232net_rxbuf_pending(int fd)
{
    return fds[fd].rxbuf_len - fds[fd].rxbuf_pos;
}

size_t rs232net_rxbuf_free(int fd)
{
    size_t pending = rs232net_rxbuf_pending(fd);

    if (pending >= sizeof fds[fd].rxbuf) {
        return 0;
    }
    return sizeof fds[fd].rxbuf - pending;
}

void rs232net_compact_rxbuf(int fd)
{
    size_t pending = rs232net_rxbuf_pending(fd);

    if (pending && fds[fd].rxbuf_pos) {
        memmove(fds[fd].rxbuf, fds[fd].rxbuf + fds[fd].rxbuf_pos, pending);
    }
    fds[fd].rxbuf_pos = 0;
    fds[fd].rxbuf_len = pending;
}

int rs232net_append_rxbuf(int fd, const uint8_t *data, size_t len)
{
    size_t free_bytes;
    size_t original_len = len;

    if (fd < 0 || fd >= RS232_NUM_DEVICES || !data || len == 0) {
        return 0;
    }

    rs232net_compact_rxbuf(fd);

    free_bytes = sizeof fds[fd].rxbuf - fds[fd].rxbuf_len;
    if (len > free_bytes) {
        len = free_bytes;
    }
    if (!len) {
        rs232net_flow_update(fd, "rxbuf-full");
#ifdef BMC64_DEBUG_PROFILE
        rs232net_dbg_rx_drop_count++;
        if (rs232net_dbg_should_log(rs232net_dbg_rx_drop_count)) {
            BMC64_RS232_DEBUG("rxbuf full fd %d drop %lu pending %lu/%lu",
                              fd, (unsigned long)original_len,
                              (unsigned long)rs232net_rxbuf_pending(fd),
                              (unsigned long)sizeof fds[fd].rxbuf);
        }
#endif
        return -1;
    }

    memcpy(fds[fd].rxbuf + fds[fd].rxbuf_len, data, len);
    fds[fd].rxbuf_len += len;
#ifdef BMC64_DEBUG_PROFILE
    if (len < original_len) {
        rs232net_flow_update(fd, "rxbuf-clipped");
        rs232net_dbg_rx_drop_count++;
        if (rs232net_dbg_should_log(rs232net_dbg_rx_drop_count)) {
            BMC64_RS232_DEBUG("rxbuf clipped fd %d append %lu/%lu pending %lu/%lu",
                              fd, (unsigned long)len,
                              (unsigned long)original_len,
                              (unsigned long)rs232net_rxbuf_pending(fd),
                              (unsigned long)sizeof fds[fd].rxbuf);
        }
    }
#endif
    return (int)len;
}

int rs232net_fill_rxbuf_from_socket(int fd, const char *tag)
{
    size_t pending;
    size_t free_bytes;
    ssize_t count;
    int ret;
#ifdef BMC64_DEBUG_PROFILE
    unsigned long now;
#endif

    if (fd < 0 || fd >= RS232_NUM_DEVICES) {
        return 0;
    }

    rs232net_poll_async(fd);

#ifdef RASPI_COMPILE
    if (fds[fd].async) {
        if (rs232net_async_status(fd) == BMC64_ASYNC_NET_CONNECTING) {
            return 0;
        }
    } else
#endif
    if (!fds[fd].fd) {
        return 0;
    }

    pending = rs232net_rxbuf_pending(fd);
    if (pending > RS232NET_RXBUF_REFILL_THRESHOLD) {
        return 0;
    }

    rs232net_compact_rxbuf(fd);
    free_bytes = sizeof fds[fd].rxbuf - fds[fd].rxbuf_len;
    if (free_bytes < RS232NET_CIRCLE_FRAME_BUFFER_SIZE) {
        rs232net_flow_update(fd, tag ? tag : "rx-wait");
#ifdef BMC64_DEBUG_PROFILE
        rs232net_dbg_rx_wait_count++;
        if (rs232net_dbg_should_log(rs232net_dbg_rx_wait_count)) {
            BMC64_RS232_DEBUG("socket-rx-wait %s fd %d free %lu min %u pending %lu/%lu",
                              tag ? tag : "rx", fd,
                              (unsigned long)free_bytes,
                              RS232NET_CIRCLE_FRAME_BUFFER_SIZE,
                              (unsigned long)rs232net_rxbuf_pending(fd),
                              (unsigned long)sizeof fds[fd].rxbuf);
        }
#endif
        return 0;
    }
    if (free_bytes > RS232NET_RXBUF_READ_CHUNK) {
        free_bytes = RS232NET_RXBUF_READ_CHUNK;
    }

#ifdef BMC64_DEBUG_PROFILE
    now = circle_get_ticks();
#endif
#ifdef RASPI_COMPILE
    if (fds[fd].async) {
        int status = bmc64_async_net_status(fds[fd].async);
        ret = (status == BMC64_ASYNC_NET_CONNECTED ||
               status == BMC64_ASYNC_NET_CLOSED ||
               status == BMC64_ASYNC_NET_ERROR)
                  ? 1 : 0;
    } else
#endif
    {
        ret = vice_network_select_poll_one(fds[fd].fd);
    }
#ifdef BMC64_DEBUG_PROFILE
    rs232net_dbg_poll_count++;
    if (rs232net_dbg_should_log(rs232net_dbg_poll_count)) {
        BMC64_RS232_TRACE("socket-poll #%u %s fd %d dev %d ret %d socket %d "
                          "buf %lu/%lu free %lu clk %lu",
                          rs232net_dbg_poll_count, tag ? tag : "rx", fd,
                          fds[fd].device, ret, fds[fd].fd ? 1 : 0,
                          (unsigned long)fds[fd].rxbuf_pos,
                          (unsigned long)fds[fd].rxbuf_len,
                          (unsigned long)free_bytes, now);
    }
    if (ret == 0) {
        fds[fd].dbg_idle_poll_streak++;
        if (fds[fd].dbg_idle_poll_streak == 256 ||
            (fds[fd].dbg_idle_poll_streak % 1024) == 0) {
            unsigned long since_rx = fds[fd].dbg_last_rx_fill_clk ?
                now - fds[fd].dbg_last_rx_fill_clk : 0;
            unsigned long since_tx = fds[fd].dbg_last_tx_clk ?
                now - fds[fd].dbg_last_tx_clk : 0;
            (void)since_rx;
            (void)since_tx;
            BMC64_RS232_DEBUG("socket-idle %s fd %d polls %u since_rx_clk %lu "
                              "since_tx_clk %lu pending %lu/%lu total_rx %lu "
                              "total_tx %lu clk %lu",
                              tag ? tag : "rx", fd,
                              fds[fd].dbg_idle_poll_streak, since_rx, since_tx,
                              (unsigned long)rs232net_rxbuf_pending(fd),
                              (unsigned long)sizeof fds[fd].rxbuf,
                              fds[fd].dbg_rx_total, fds[fd].dbg_tx_total, now);
            fds[fd].dbg_last_idle_report_clk = now;
        }
    }
#endif
    if (ret <= 0) {
        return ret;
    }

#ifdef RASPI_COMPILE
    if (fds[fd].async) {
        count = bmc64_async_net_receive(fds[fd].async,
                                        fds[fd].rxbuf + fds[fd].rxbuf_len,
                                        free_bytes);
        if (count == 0) {
            int status = bmc64_async_net_status(fds[fd].async);
            if (status == BMC64_ASYNC_NET_CONNECTED ||
                status == BMC64_ASYNC_NET_CONNECTING) {
                return 0;
            }
        }
    } else
#endif
    {
        count = vice_network_receive(fds[fd].fd,
                                     fds[fd].rxbuf + fds[fd].rxbuf_len,
                                     free_bytes, 0);
    }
    if (count <= 0) {
        if (count < 0) {
            log_error(rs232net_log, "Error reading: %d.",
                      vice_network_get_errorcode());
#ifdef BMC64_DEBUG_PROFILE
            BMC64_RS232_EVENT("socket-rx-error %s fd %d err %d",
                              tag ? tag : "rx", fd,
                              vice_network_get_errorcode());
#endif
            if (rs232net_rxbuf_pending(fd) > 0) {
                rs232net_mark_disconnect_pending(fd, tag ? tag : "rx-error");
                return -1;
            }
            if (fds[fd].hayes) {
                mdm_disconnect(&fds[fd].modem, TRUE);
            } else {
                rs232net_closesocket(fd);
            }
            return -1;
        }
        log_error(rs232net_log, "EOF.");
#ifdef BMC64_DEBUG_PROFILE
        BMC64_RS232_EVENT("socket-rx-eof %s fd %d", tag ? tag : "rx", fd);
#endif
        if (rs232net_rxbuf_pending(fd) > 0) {
            rs232net_mark_disconnect_pending(fd, tag ? tag : "rx");
            return -1;
        }
        if (fds[fd].hayes) {
            mdm_disconnect(&fds[fd].modem, TRUE);
        } else {
            rs232net_closesocket(fd);
        }
        return -1;
    }

    fds[fd].rxbuf_len += (size_t)count;
    rs232net_flow_update(fd, tag ? tag : "rx-fill");
#ifdef BMC64_DEBUG_PROFILE
    rs232net_dbg_rx_fill_count++;
    now = circle_get_ticks();
    {
        unsigned long dt_rx = fds[fd].dbg_last_rx_fill_clk ?
            now - fds[fd].dbg_last_rx_fill_clk : 0;
        unsigned long since_tx = fds[fd].dbg_last_tx_clk ?
            now - fds[fd].dbg_last_tx_clk : 0;
        unsigned int idle_polls = fds[fd].dbg_idle_poll_streak;
        (void)dt_rx;
        (void)since_tx;
        (void)idle_polls;

        fds[fd].dbg_rx_total += (unsigned long)count;
        fds[fd].dbg_last_rx_fill_clk = now;
        fds[fd].dbg_idle_poll_streak = 0;

        BMC64_RS232_DEBUG("socket-rx-fill #%u %s fd %d dev %d bytes %ld "
                          "pending %lu/%lu total_rx %lu dt_rx_clk %lu "
                          "since_tx_clk %lu idle_polls %u clk %lu",
                          rs232net_dbg_rx_fill_count, tag ? tag : "rx", fd,
                          fds[fd].device, (long)count,
                          (unsigned long)rs232net_rxbuf_pending(fd),
                          (unsigned long)sizeof fds[fd].rxbuf,
                          fds[fd].dbg_rx_total, dt_rx, since_tx, idle_polls,
                          now);
        rs232net_dbg_dump_sample("socket-rx-sample", fd,
                                 fds[fd].rxbuf + fds[fd].rxbuf_len - (size_t)count,
                                 (size_t)count);
    }
#endif
    return (int)count;
}

#endif
