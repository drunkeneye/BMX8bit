/*
 * rs232_hayes_modem.c - Hayes/tcpser modem integration.
 */

#include "vice.h"

#ifdef HAVE_RS232NET

#include "bmc64_log.h"
#include "rs232_hayes_modem.h"
#include "rs232net_debug.h"
#include "rs232net_internal.h"

#ifdef RASPI_COMPILE
static void rs232net_hayes_audio_stop(const char *reason)
{
#ifdef BMC64_DEBUG_PROFILE
    BMC64_RS232_DEBUG("modem sound stop reason %s",
                      reason ? reason : "unspecified");
#else
    (void)reason;
#endif
    bmc64_modem_audio_stop();
}
#endif

void rs232net_hayes_init_fd(int fd)
{
    mdm_init_config(&fds[fd].modem);
    fds[fd].modem.dce_data.ifd = fd;
    fds[fd].modem.dce_data.ofd = fd;
    fds[fd].modem.line_data.fd = fd;
    fds[fd].modem.line_speed = 2400;
    mdm_set_control_lines(&fds[fd].modem);
}

void rs232net_modem_connect_succeeded(int fd)
{
    fds[fd].modem_connect_pending = 0;
    fds[fd].deferred = 0;
    fds[fd].modem.line_data.is_connected = TRUE;
    fds[fd].modem.conn_type = MDM_CONN_OUTGOING;
    mdm_set_control_lines(&fds[fd].modem);
    mdm_print_speed(&fds[fd].modem);
#ifdef BMC64_DEBUG_PROFILE
    BMC64_RS232_DEBUG("hayes async connected fd %d", fd);
#endif
}

void rs232net_modem_connect_failed(int fd)
{
    fds[fd].modem_connect_pending = 0;
    fds[fd].deferred = 1;
    fds[fd].modem.line_data.is_connected = FALSE;
    fds[fd].modem.is_cmd_mode = TRUE;
#ifdef RASPI_COMPILE
    rs232net_hayes_audio_stop("connect-failed");
#endif
    mdm_send_response(MDM_RESP_NO_CARRIER, &fds[fd].modem);
#ifdef BMC64_DEBUG_PROFILE
    BMC64_RS232_DEBUG("hayes async failed fd %d", fd);
#endif
}

int rs232net_modem_push_to_dte(int fd, const unsigned char *data, int len)
{
    int rc;

#ifdef BMC64_DEBUG_PROFILE
    if (fd >= 0 && fd < RS232_NUM_DEVICES && data && len > 0) {
        BMC64_RS232_DEBUG("modem-dte-push fd %d bytes %d pending_before %lu/%lu",
                          fd, len, (unsigned long)rs232net_rxbuf_pending(fd),
                          (unsigned long)sizeof fds[fd].rxbuf);
        rs232net_dbg_dump_sample("modem-dte-sample", fd, data, (size_t)len);
    }
#endif
    rc = rs232net_append_rxbuf(fd, data, len < 0 ? 0 : (size_t)len);
    rs232net_flow_update(fd, "modem-dte-push");
    return rc;
}

int rs232net_modem_line_connect(int fd, const char *target)
{
    int rc;

    if (!rs232net_fd_is_open(fd)) {
#ifdef BMC64_DEBUG_PROFILE
        BMC64_RS232_EVENT("hayes dial invalid fd %d target '%s'",
                          fd, target ? target : "");
#endif
        return -1;
    }
#ifdef RASPI_COMPILE
    rs232net_hayes_audio_stop("new-dial");
#endif
    if (fds[fd].fd) {
        rs232net_closesocket(fd);
    }
#ifdef RASPI_COMPILE
    if (fds[fd].async) {
        rs232net_async_stop(fd);
    }
#endif
    fds[fd].deferred = 1;
    fds[fd].modem_connect_pending = 1;
    BMC64_RS232_EVENT("hayes dial %s", target);
#ifdef RASPI_COMPILE
    if (rs232net_hayes_audio_mode > 0) {
        bmc64_modem_audio_play_dial_blocking();
        if (rs232net_hayes_audio_mode == RS232NET_HAYES_AUDIO_SHORT ||
            rs232net_hayes_audio_mode == RS232NET_HAYES_AUDIO_LONG) {
            bmc64_modem_audio_play_connect_blocking();
        }
    }
#endif
    rc = rs232net_connect_target(fd, target);
    if (rc < 0) {
        fds[fd].modem_connect_pending = 0;
    }
#ifdef BMC64_DEBUG_PROFILE
    BMC64_RS232_DEBUG("hayes dial result fd %d target '%s' rc %d socket %d deferred %d",
                      fd, target ? target : "", rc, fds[fd].fd ? 1 : 0,
                      fds[fd].deferred);
#endif
    return rc;
}

int rs232net_modem_line_write(int fd, const unsigned char *data, int len)
{
    int i;

    if (!rs232net_fd_is_open(fd) || len < 0) {
        return -1;
    }

    for (i = 0; i < len; ++i) {
        if (rs232net_raw_putc(fd, data[i]) < 0) {
            return -1;
        }
    }
    return len;
}

void rs232net_modem_line_disconnect(int fd)
{
    if (!rs232net_fd_is_open(fd)) {
        return;
    }
#ifdef RASPI_COMPILE
    rs232net_hayes_audio_stop("disconnect");
#endif
    rs232net_disconnect_deferred(fd, "hayes-disconnect");
}

void rs232net_modem_set_flow_control(int fd, int opts)
{
    if (!rs232net_fd_is_open(fd)) {
        return;
    }
    rs232net_flow_set_options(fd, opts);
#ifdef BMC64_DEBUG_PROFILE
    BMC64_RS232_DEBUG("hayes flow-control fd %d opts 0x%02x rtscts %d xonxoff %d",
                      fd, opts, (opts & MDM_FC_RTS) ? 1 : 0,
                      (opts & MDM_FC_XON) ? 1 : 0);
#endif
}

int rs232net_hayes_putc(int fd, uint8_t b)
{
    if (rs232net_flow_handle_dte_char(fd, b)) {
        return 0;
    }
    mdm_parse_data(&fds[fd].modem, &b, 1);
    return 0;
}

int rs232net_hayes_getc(int fd, uint8_t *b)
{
    if (fds[fd].rxbuf_pos >= fds[fd].rxbuf_len) {
        rs232net_clear_rxbuf(fd);
        if (!rs232net_flow_dce_to_dte_paused(fd, "hayes")) {
            rs232net_fill_rxbuf_from_socket(fd, "hayes");
        }
    } else if (!rs232net_flow_dce_to_dte_paused(fd, "hayes-prefetch")) {
        rs232net_fill_rxbuf_from_socket(fd, "hayes-prefetch");
    }

    if (fds[fd].rxbuf_pos >= fds[fd].rxbuf_len) {
        return 0;
    }

    *b = fds[fd].rxbuf[fds[fd].rxbuf_pos++];
    rs232net_complete_pending_disconnect(fd);
    rs232net_flow_update(fd, "hayes-rx");
#ifdef BMC64_DEBUG_PROFILE
    rs232net_dbg_rx_count++;
    if (rs232net_dbg_should_log(rs232net_dbg_rx_count)) {
        BMC64_RS232_TRACE("hayes-rx #%u fd %d byte 0x%02x '%c'",
                          rs232net_dbg_rx_count, fd, *b,
                          rs232net_dbg_chr(*b));
    }
#endif
    return 1;
}

int rs232net_hayes_set_status(int fd, enum rs232handshake_out status)
{
    int dtr = (status & RS232_HSO_DTR) ? 1 : 0;
    int rts = (status & RS232_HSO_RTS) ? 1 : 0;

    fds[fd].modem.dce_data.ip232_dtr = dtr;
    if (!dtr && fds[fd].modem_connect_pending) {
        fds[fd].modem_connect_pending = 0;
        fds[fd].modem.is_cmd_mode = TRUE;
#ifdef RASPI_COMPILE
        rs232net_async_stop(fd);
        rs232net_hayes_audio_stop("dtr-low-pending");
#endif
    }
    if (!dtr && fds[fd].modem.conn_type != MDM_CONN_NONE) {
        mdm_disconnect(&fds[fd].modem, TRUE);
    }
#ifdef BMC64_DEBUG_PROFILE
    rs232net_dbg_status_count++;
    if (dtr != fds[fd].dtr_out ||
        rs232net_dbg_should_log_status(fd, status, rs232net_dbg_status_count)) {
        BMC64_RS232_DEBUG("hayes set-status #%u fd %d raw 0x%02x dtr %d rts %d",
                          rs232net_dbg_status_count, fd, status, dtr, rts);
    }
#endif
    fds[fd].dtr_out = dtr;
    fds[fd].rts_out = rts;
    rs232net_flow_update(fd, "hayes-set-status");
    return 0;
}

enum rs232handshake_in rs232net_hayes_get_status(int fd)
{
    enum rs232handshake_in status = 0;

    rs232net_poll_async(fd);
    rs232net_flow_update(fd, "hayes-get-status");
    status |= RS232_HSI_DSR;
    if (fds[fd].modem.dce_data.ip232_cts &&
        rs232net_flow_cts_active(fd)) {
        status |= RS232_HSI_CTS;
    }
    if (fds[fd].modem.dce_data.ip232_dcd) {
        status |= RS232_HSI_DCD;
    }
    if (fds[fd].modem.dce_data.ip232_ri) {
        status |= RS232_HSI_RI;
    }
#ifdef BMC64_DEBUG_PROFILE
    {
        static enum rs232handshake_in dbg_old_hayes_status[RS232_NUM_DEVICES];
        if (status != dbg_old_hayes_status[fd]) {
            BMC64_RS232_DEBUG("hayes get-status fd %d status 0x%02x cts %d dsr %d dcd %d ri %d",
                              fd, status,
                              (status & RS232_HSI_CTS) ? 1 : 0,
                              (status & RS232_HSI_DSR) ? 1 : 0,
                              (status & RS232_HSI_DCD) ? 1 : 0,
                              (status & RS232_HSI_RI) ? 1 : 0);
            dbg_old_hayes_status[fd] = status;
        }
    }
#endif
    return status;
}

#endif
