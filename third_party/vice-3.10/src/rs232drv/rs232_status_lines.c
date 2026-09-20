/*
 * rs232_status_lines.c - RS232 network status-line handling.
 */

#include "vice.h"

#ifdef HAVE_RS232NET

#include <stdio.h>

#include "bmc64_log.h"
#include "rs232net.h"
#include "rs232net_debug.h"
#include "rs232net_internal.h"

/* #define LOG_MODEM_STATUS */

static size_t rs232net_min_size(size_t a, size_t b)
{
    return a < b ? a : b;
}

static size_t rs232net_async_rx_free_or_max(int fd)
{
#ifdef RASPI_COMPILE
    if (fds[fd].async) {
        return bmc64_async_net_rx_free(fds[fd].async);
    }
#else
    (void)fd;
#endif
    return (size_t)-1;
}

static size_t rs232net_async_tx_free_or_max(int fd)
{
#ifdef RASPI_COMPILE
    if (fds[fd].async) {
        return bmc64_async_net_tx_free(fds[fd].async);
    }
#else
    (void)fd;
#endif
    return (size_t)-1;
}

static int rs232net_flow_rtscts_enabled(int fd)
{
    return fds[fd].hayes && (fds[fd].flow_control_opts & MDM_FC_RTS);
}

static int rs232net_flow_xonxoff_enabled(int fd)
{
    return fds[fd].hayes && (fds[fd].flow_control_opts & MDM_FC_XON);
}

static int rs232net_flow_queue_control_char(int fd, uint8_t b,
                                            const char *tag)
{
    if (fd < 0 || fd >= RS232_NUM_DEVICES || !fds[fd].inuse) {
        return -1;
    }

    rs232net_compact_rxbuf(fd);
    if (fds[fd].rxbuf_len >= sizeof fds[fd].rxbuf) {
#ifdef BMC64_DEBUG_PROFILE
        BMC64_RS232_EVENT("flow-char-drop %s fd %d byte 0x%02x pending %lu/%lu",
                          tag ? tag : "flow", fd, b,
                          (unsigned long)rs232net_rxbuf_pending(fd),
                          (unsigned long)sizeof fds[fd].rxbuf);
#else
        (void)tag;
#endif
        return -1;
    }

    fds[fd].rxbuf[fds[fd].rxbuf_len++] = b;
    return 0;
}

static void rs232net_flow_update_xonxoff(int fd, size_t tx_free,
                                         const char *tag)
{
    if (!rs232net_flow_xonxoff_enabled(fd)) {
        return;
    }

    if (fds[fd].tx_flow_paused && !fds[fd].xonxoff_xoff_sent) {
        if (rs232net_flow_queue_control_char(fd, RS232NET_XOFF, tag) == 0) {
            fds[fd].xonxoff_xoff_sent = 1;
#ifdef BMC64_DEBUG_PROFILE
            fds[fd].dbg_xoff_sent_count++;
            BMC64_RS232_EVENT("flow-xoff-send %s fd %d count %u tx_free %lu",
                              tag ? tag : "flow", fd,
                              fds[fd].dbg_xoff_sent_count,
                              tx_free == (size_t)-1 ? 0 : (unsigned long)tx_free);
#endif
        }
    } else if (!fds[fd].tx_flow_paused && fds[fd].xonxoff_xoff_sent &&
               tx_free >= RS232NET_TX_FLOW_ASSERT_FREE) {
        if (rs232net_flow_queue_control_char(fd, RS232NET_XON, tag) == 0) {
            fds[fd].xonxoff_xoff_sent = 0;
#ifdef BMC64_DEBUG_PROFILE
            fds[fd].dbg_xon_sent_count++;
            BMC64_RS232_EVENT("flow-xon-send %s fd %d count %u tx_free %lu",
                              tag ? tag : "flow", fd,
                              fds[fd].dbg_xon_sent_count,
                              tx_free == (size_t)-1 ? 0 : (unsigned long)tx_free);
#endif
        }
    }
}

void rs232net_flow_reset(int fd)
{
    if (fd < 0 || fd >= RS232_NUM_DEVICES) {
        return;
    }

    fds[fd].rts_out = 0;
    fds[fd].cts_in = 1;
    fds[fd].cts_backpressure = 0;
    fds[fd].rx_flow_paused = 0;
    fds[fd].tx_flow_paused = 0;
    fds[fd].flow_control_opts = 0;
    fds[fd].xonxoff_dte_paused = 0;
    fds[fd].xonxoff_xoff_sent = 0;
#ifdef BMC64_DEBUG_PROFILE
    fds[fd].dbg_cts_assert_count = 0;
    fds[fd].dbg_cts_deassert_count = 0;
    fds[fd].dbg_tx_queue_full_count = 0;
    fds[fd].dbg_xon_sent_count = 0;
    fds[fd].dbg_xoff_sent_count = 0;
    fds[fd].dbg_xon_received_count = 0;
    fds[fd].dbg_xoff_received_count = 0;
    fds[fd].dbg_flow_gate_count = 0;
#endif
}

void rs232net_flow_set_options(int fd, int opts)
{
    int old_opts;

    if (fd < 0 || fd >= RS232_NUM_DEVICES || !fds[fd].inuse) {
        return;
    }

    opts &= MDM_FC_RTS | MDM_FC_XON;
    old_opts = fds[fd].flow_control_opts;

    if (!(opts & MDM_FC_XON)) {
        fds[fd].xonxoff_dte_paused = 0;
        if ((old_opts & MDM_FC_XON) && fds[fd].xonxoff_xoff_sent) {
            if (rs232net_flow_queue_control_char(fd, RS232NET_XON,
                                                 "flow-disable") == 0) {
#ifdef BMC64_DEBUG_PROFILE
                fds[fd].dbg_xon_sent_count++;
                BMC64_RS232_EVENT("flow-xon-send flow-disable fd %d count %u",
                                  fd, fds[fd].dbg_xon_sent_count);
#endif
            }
        }
        fds[fd].xonxoff_xoff_sent = 0;
    }

    fds[fd].flow_control_opts = opts;
#ifdef BMC64_DEBUG_PROFILE
    BMC64_RS232_DEBUG("flow-options fd %d old 0x%02x new 0x%02x rtscts %d xonxoff %d",
                      fd, old_opts, opts, (opts & MDM_FC_RTS) ? 1 : 0,
                      (opts & MDM_FC_XON) ? 1 : 0);
#endif
    rs232net_flow_update(fd, "flow-options");
}

int rs232net_flow_update(int fd, const char *tag)
{
    size_t rx_free;
    size_t tx_free;
    int old_cts;
    int new_cts;

    if (fd < 0 || fd >= RS232_NUM_DEVICES || !fds[fd].inuse) {
        return 0;
    }

    rx_free = rs232net_min_size(rs232net_rxbuf_free(fd),
                                rs232net_async_rx_free_or_max(fd));
    tx_free = rs232net_async_tx_free_or_max(fd);
    old_cts = fds[fd].cts_in;

    if (!fds[fd].rx_flow_paused &&
        rx_free <= RS232NET_RX_FLOW_DEASSERT_FREE) {
        fds[fd].rx_flow_paused = 1;
    } else if (fds[fd].rx_flow_paused &&
               rx_free >= RS232NET_RX_FLOW_ASSERT_FREE) {
        fds[fd].rx_flow_paused = 0;
    }

    if (!fds[fd].tx_flow_paused &&
        tx_free <= RS232NET_TX_FLOW_DEASSERT_FREE) {
        fds[fd].tx_flow_paused = 1;
    } else if (fds[fd].tx_flow_paused &&
               tx_free >= RS232NET_TX_FLOW_ASSERT_FREE) {
        fds[fd].tx_flow_paused = 0;
    }

    fds[fd].cts_backpressure =
        fds[fd].rx_flow_paused || fds[fd].tx_flow_paused;
    new_cts = fds[fd].cts_backpressure ? 0 : 1;
    fds[fd].cts_in = new_cts;
    rs232net_flow_update_xonxoff(fd, tx_free, tag);

#ifdef BMC64_DEBUG_PROFILE
    if (new_cts != old_cts) {
        if (new_cts) {
            fds[fd].dbg_cts_assert_count++;
        } else {
            fds[fd].dbg_cts_deassert_count++;
        }
        BMC64_RS232_EVENT("flow-cts-%s %s fd %d rx_free %lu tx_free %lu "
                          "rx_pause %d tx_pause %d rtscts %d xonxoff %d "
                          "assert %u deassert %u",
                          new_cts ? "assert" : "deassert",
                          tag ? tag : "flow", fd,
                          (unsigned long)rx_free,
                          tx_free == (size_t)-1 ? 0 : (unsigned long)tx_free,
                          fds[fd].rx_flow_paused,
                          fds[fd].tx_flow_paused,
                          rs232net_flow_rtscts_enabled(fd) ? 1 : 0,
                          rs232net_flow_xonxoff_enabled(fd) ? 1 : 0,
                          fds[fd].dbg_cts_assert_count,
                          fds[fd].dbg_cts_deassert_count);
    }
#else
    (void)tag;
#endif

    return new_cts;
}

int rs232net_flow_cts_active(int fd)
{
    if (fd < 0 || fd >= RS232_NUM_DEVICES) {
        return 0;
    }
    return fds[fd].cts_in ? 1 : 0;
}

void rs232net_flow_note_tx_queue_full(int fd)
{
    if (fd < 0 || fd >= RS232_NUM_DEVICES || !fds[fd].inuse) {
        return;
    }

    fds[fd].tx_flow_paused = 1;
    fds[fd].cts_backpressure = 1;
    if (fds[fd].cts_in) {
        fds[fd].cts_in = 0;
#ifdef BMC64_DEBUG_PROFILE
        fds[fd].dbg_cts_deassert_count++;
#endif
    }
#ifdef BMC64_DEBUG_PROFILE
    fds[fd].dbg_tx_queue_full_count++;
    BMC64_RS232_EVENT("flow-tx-full fd %d count %u",
                      fd, fds[fd].dbg_tx_queue_full_count);
#endif
    rs232net_flow_update_xonxoff(fd, rs232net_async_tx_free_or_max(fd),
                                 "tx-full");
}

int rs232net_flow_dce_to_dte_paused(int fd, const char *tag)
{
    const char *reason = NULL;

    if (fd < 0 || fd >= RS232_NUM_DEVICES || !fds[fd].inuse ||
        !fds[fd].hayes || fds[fd].modem.is_cmd_mode) {
        return 0;
    }

    if (rs232net_flow_rtscts_enabled(fd) && !fds[fd].rts_out) {
        reason = "rts-low";
    } else if (rs232net_flow_xonxoff_enabled(fd) &&
               fds[fd].xonxoff_dte_paused) {
        reason = "dte-xoff";
    }

    if (!reason) {
        return 0;
    }

#ifdef BMC64_DEBUG_PROFILE
    fds[fd].dbg_flow_gate_count++;
    if (rs232net_dbg_should_log(fds[fd].dbg_flow_gate_count)) {
        BMC64_RS232_DEBUG("flow-gate %s fd %d reason %s count %u rts %d opts 0x%02x",
                          tag ? tag : "rx", fd, reason,
                          fds[fd].dbg_flow_gate_count, fds[fd].rts_out,
                          fds[fd].flow_control_opts);
    }
#else
    (void)tag;
#endif
    return 1;
}

int rs232net_flow_handle_dte_char(int fd, uint8_t b)
{
    uint8_t ch;

    if (fd < 0 || fd >= RS232_NUM_DEVICES || !fds[fd].inuse ||
        !rs232net_flow_xonxoff_enabled(fd) || fds[fd].modem.is_cmd_mode) {
        return 0;
    }

    ch = (uint8_t)dce_strip_parity(&fds[fd].modem.dce_data, b);
    if (ch == RS232NET_XOFF) {
        fds[fd].xonxoff_dte_paused = 1;
#ifdef BMC64_DEBUG_PROFILE
        fds[fd].dbg_xoff_received_count++;
        BMC64_RS232_EVENT("flow-xoff-recv fd %d count %u",
                          fd, fds[fd].dbg_xoff_received_count);
#endif
        return 1;
    }
    if (ch == RS232NET_XON) {
        fds[fd].xonxoff_dte_paused = 0;
#ifdef BMC64_DEBUG_PROFILE
        fds[fd].dbg_xon_received_count++;
        BMC64_RS232_EVENT("flow-xon-recv fd %d count %u",
                          fd, fds[fd].dbg_xon_received_count);
#endif
        return 1;
    }

    return 0;
}

int rs232net_raw_set_status(int fd, enum rs232handshake_out status)
{
    int dtr = (status & RS232_HSO_DTR) ? 1 : 0;
    int rts = (status & RS232_HSO_RTS) ? 1 : 0;

#ifdef LOG_MODEM_STATUS
    if (dtr != fds[fd].dtr_out || rts != fds[fd].rts_out) {
        DEBUG_LOG_MESSAGE((rs232net_log, "rs232net_set_status(fd:%d) status:%02x dtr:%d rts:%d",
            fd, status, dtr, rts
        ));
    }
#endif
    if (fds[fd].useip232 && fds[fd].fd) {
        if (dtr != fds[fd].dtr_out) {
            if (dtr) {
                rs232net_raw_putc(fd, IP232MAGIC);
                rs232net_raw_putc(fd, IP232DTRHI);
            }
            if (!dtr) {
                rs232net_raw_putc(fd, IP232MAGIC);
                rs232net_raw_putc(fd, IP232DTRLO);
            }
        }
    }
#ifdef BMC64_DEBUG_PROFILE
    rs232net_dbg_status_count++;
    if (dtr != fds[fd].dtr_out ||
        rs232net_dbg_should_log_status(fd, status, rs232net_dbg_status_count)) {
        BMC64_RS232_DEBUG("set-status #%u fd %d raw 0x%02x dtr %d rts %d ip232 %d",
                          rs232net_dbg_status_count, fd, status, dtr,
                          rts, fds[fd].useip232);
    }
#endif
    if (!dtr && (fds[fd].fd || !fds[fd].deferred)) {
        rs232net_disconnect_deferred(fd, "dtr-low");
    }
    fds[fd].dtr_out = dtr;
    fds[fd].rts_out = rts;
    rs232net_flow_update(fd, "raw-set-status");
    return 0;
}

enum rs232handshake_in rs232net_raw_get_status(int fd)
{
    enum rs232handshake_in status = 0;
#ifdef LOG_MODEM_STATUS
    static enum rs232handshake_in oldstatus = 0;
#endif

    rs232net_poll_async(fd);
    rs232net_flow_update(fd, "raw-get-status");

    if (fds[fd].useip232) {
        if (rs232net_link_active(fd) && fds[fd].dtr_out) {
            status |= RS232_HSI_DSR;
            if (rs232net_flow_cts_active(fd)) {
                status |= RS232_HSI_CTS;
            }
        }
        if (fds[fd].dcd_in && fds[fd].dtr_out && rs232net_link_active(fd)) {
            status |= RS232_HSI_DCD;
        }
        if (fds[fd].ri_in) {
            status |= RS232_HSI_RI;
        }
    } else {
        if (rs232net_link_active(fd) && fds[fd].dtr_out) {
            status |= RS232_HSI_DSR | RS232_HSI_DCD;
            if (rs232net_flow_cts_active(fd)) {
                status |= RS232_HSI_CTS;
            }
        }
    }

#ifdef BMC64_DEBUG_PROFILE
    {
        static enum rs232handshake_in dbg_oldstatus[RS232_NUM_DEVICES];
        if (status != dbg_oldstatus[fd]) {
            BMC64_RS232_DEBUG("get-status fd %d status 0x%02x cts %d dsr %d dcd %d ri %d ip232 %d",
                              fd, status,
                              (status & RS232_HSI_CTS) ? 1 : 0,
                              (status & RS232_HSI_DSR) ? 1 : 0,
                              (status & RS232_HSI_DCD) ? 1 : 0,
                              (status & RS232_HSI_RI) ? 1 : 0,
                              fds[fd].useip232);
            dbg_oldstatus[fd] = status;
        }
    }
#endif

#ifdef LOG_MODEM_STATUS
    if (status != oldstatus) {
        printf("rs232net_get_status(fd:%d): DCD:%d modem_status:%02x cts:%d dsr:%d dcd:%d ri:%d\n",
               fd, fds[fd].dcd_in, status,
               status & RS232_HSI_CTS ? 1 : 0,
               status & RS232_HSI_DSR ? 1 : 0,
               status & RS232_HSI_DCD ? 1 : 0,
               status & RS232_HSI_RI ? 1 : 0
              );
        oldstatus = status;
    }
#endif
    return status;
}

int rs232net_set_status(int fd, enum rs232handshake_out status)
{
    if (!rs232net_fd_is_valid(fd)) {
        log_error(rs232net_log, "Attempt to set status on invalid fd %d.", fd);
        return -1;
    }
    if (!rs232net_fd_is_open(fd)) {
        log_error(rs232net_log, "Attempt to set status on non-open fd %d.", fd);
        return -1;
    }

    if (rs232net_fd_is_hayes(fd)) {
        return rs232net_hayes_set_status(fd, status);
    }
    return rs232net_raw_set_status(fd, status);
}

enum rs232handshake_in rs232net_get_status(int fd)
{
    if (!rs232net_fd_is_open(fd)) {
        return 0;
    }

    if (rs232net_fd_is_hayes(fd)) {
        return rs232net_hayes_get_status(fd);
    }
    return rs232net_raw_get_status(fd);
}

#endif
