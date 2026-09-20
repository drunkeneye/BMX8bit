/*
 * BMX adapter for the imported tcpser modem core.
 *
 * The original tcpser DCE side talks to a POSIX serial device and the line
 * side talks to POSIX sockets. BMX replaces both with callbacks into VICE's
 * RS232 network backend.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dce.h"
#include "line.h"
#include "modem_core.h"
#include "phone_book.h"
#include "rs232_hayes_modem.h"

void nvt_init_config(nvt_vars *vars)
{
    int i;

    vars->binary_xmit = FALSE;
    vars->binary_recv = FALSE;
    for (i = 0; i < 256; ++i) {
        vars->term[i] = 0;
    }
}

int usleep(useconds_t useconds)
{
    (void)useconds;
    return 0;
}

void dce_init_config(dce_config *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->parity = -1;
    cfg->ifd = -1;
    cfg->ofd = -1;
}

int dce_connect(dce_config *cfg)
{
    (void)cfg;
    return 0;
}

int dce_set_flow_control(dce_config *cfg, int opts)
{
    opts &= MDM_FC_RTS | MDM_FC_XON;
    cfg->flow_control_opts = opts;
    rs232net_modem_set_flow_control(cfg->ifd, opts);
#ifdef BMC64_DEBUG_PROFILE
    printf("tcpserdbg: flow-control fd %d opts 0x%02x rtscts %d xonxoff %d\r\n",
           cfg->ifd, opts, (opts & MDM_FC_RTS) ? 1 : 0,
           (opts & MDM_FC_XON) ? 1 : 0);
#endif
    return 0;
}

int dce_set_control_lines(dce_config *cfg, int state)
{
    cfg->ip232_dcd = (state & DCE_CL_DCD) ? 1 : 0;
    cfg->ip232_cts = (state & DCE_CL_CTS) ? 1 : 0;
    cfg->ip232_ri = (state & DCE_CL_RI) ? 1 : 0;
    return 0;
}

int dce_get_control_lines(dce_config *cfg)
{
    int state = 0;

    if (cfg->ip232_dtr) {
        state |= DCE_CL_DTR;
    }
    return state;
}

int dce_check_control_lines(dce_config *cfg)
{
    return dce_get_control_lines(cfg);
}

int dce_write(dce_config *cfg, unsigned char *data, int len)
{
    unsigned char stack_buf[256];
    unsigned char *buf = data;
    int i;
    int rc;

    if (len <= 0) {
        return 0;
    }

    if (cfg->parity > 0) {
        if (len <= (int)sizeof stack_buf) {
            buf = stack_buf;
        } else {
            buf = malloc(len);
            if (!buf) {
                return -1;
            }
        }
        for (i = 0; i < len; ++i) {
            buf[i] = apply_parity(data[i], cfg->parity);
        }
    }

    rc = rs232net_modem_push_to_dte(cfg->ifd, buf, len);

    if (buf != data && buf != stack_buf) {
        free(buf);
    }
    return rc;
}

int dce_write_char_raw(dce_config *cfg, unsigned char data)
{
    return rs232net_modem_push_to_dte(cfg->ifd, &data, 1);
}

int dce_read(dce_config *cfg, unsigned char *data, int len)
{
    (void)cfg;
    (void)data;
    (void)len;
    return 0;
}

int dce_read_char_raw(dce_config *cfg)
{
    (void)cfg;
    return 0;
}

static int detect_parity(int charA, int charT)
{
    int parity = ((charA >> 6) & 2) | (charT >> 7);
    int eobits = gen_parity(charA & 0x7f) << 1 | gen_parity(charT & 0x7f);

    if (parity == 1 || parity == 2) {
        return parity == eobits ? PARITY_EVEN : PARITY_ODD;
    }
    return parity;
}

void dce_detect_parity(dce_config *cfg, unsigned char a, unsigned char t)
{
    cfg->parity = detect_parity(a, t);
}

int dce_strip_parity(dce_config *cfg, unsigned char data)
{
    return cfg->parity ? data & 0x7f : data;
}

int dce_get_parity(dce_config *cfg)
{
    return cfg->parity;
}

static void line_reset(line_config *cfg)
{
    cfg->is_connected = FALSE;
    cfg->is_telnet = FALSE;
    cfg->is_data_received = FALSE;
}

void line_init_config(line_config *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->fd = -1;
    cfg->sfd = -1;
    nvt_init_config(&cfg->nvt_data);
    line_reset(cfg);
}

int line_init_conn(line_config *cfg)
{
    (void)cfg;
    return 0;
}

int line_read(line_config *cfg, unsigned char *data, int len)
{
    (void)cfg;
    (void)data;
    (void)len;
    return 0;
}

int line_write(line_config *cfg, unsigned char *data, int len)
{
    if (!cfg->is_connected) {
        return -1;
    }
    return rs232net_modem_line_write(cfg->fd, data, len);
}

int line_listen(line_config *cfg)
{
    (void)cfg;
    return 0;
}

int line_accept(line_config *cfg)
{
    (void)cfg;
    return -1;
}

int line_off_hook(line_config *cfg)
{
    (void)cfg;
    return 0;
}

int line_connect(line_config *cfg, char *number)
{
    char address[PH_ENTRY_SIZE];
    int rc;

    pb_search(number, address);
#ifdef BMC64_DEBUG_PROFILE
    printf("tcpserdbg: line-connect fd %d number '%s' address '%s'\r\n",
           cfg->fd, number ? number : "", address);
#endif

    rc = rs232net_modem_line_connect(cfg->fd, address);
#ifdef BMC64_DEBUG_PROFILE
    printf("tcpserdbg: line-connect result fd %d rc %d address '%s'\r\n",
           cfg->fd, rc, address);
#endif

    if (rc == 0) {
        cfg->is_connected = TRUE;
        return 0;
    }
    if (rc == LINE_CONNECT_PENDING) {
        return LINE_CONNECT_PENDING;
    }
    return -1;
}

int line_disconnect(line_config *cfg)
{
    if (cfg->fd >= 0) {
        rs232net_modem_line_disconnect(cfg->fd);
    }
    line_reset(cfg);
    return 0;
}
