/*
 * rs232net_debug.c - private RS232 network debug helpers.
 */

#include "vice.h"

#ifdef HAVE_RS232NET

#include <string.h>

#include "bmc64_log.h"
#include "rs232net_debug.h"
#include "rs232net_internal.h"

#ifdef BMC64_DEBUG_PROFILE

#define RS232NET_DBG_DUMP_LIMIT 48

unsigned int rs232net_dbg_tx_count;
unsigned int rs232net_dbg_rx_count;
unsigned int rs232net_dbg_poll_count;
unsigned int rs232net_dbg_status_count;
unsigned int rs232net_dbg_rx_fill_count;
unsigned int rs232net_dbg_rx_drop_count;
unsigned int rs232net_dbg_rx_wait_count;

int rs232net_dbg_should_log(unsigned int count)
{
    return count <= 64 || (count % 256) == 0;
}

int rs232net_dbg_should_log_status(int fd, enum rs232handshake_out status,
                                   unsigned int count)
{
    static enum rs232handshake_out last_status[RS232_NUM_DEVICES];
    static uint8_t have_status[RS232_NUM_DEVICES];

    if (fd >= 0 && fd < RS232_NUM_DEVICES &&
        (!have_status[fd] || last_status[fd] != status)) {
        have_status[fd] = 1;
        last_status[fd] = status;
        return 1;
    }

    return count <= 8 || (count % 1024) == 0;
}

char rs232net_dbg_chr(uint8_t b)
{
    return (b >= 0x20 && b <= 0x7e) ? (char)b : '.';
}

void rs232net_dbg_reset_fd(int fd)
{
    if (fd < 0 || fd >= RS232_NUM_DEVICES) {
        return;
    }

    fds[fd].dbg_last_rx_fill_clk = 0;
    fds[fd].dbg_last_tx_clk = 0;
    fds[fd].dbg_last_idle_report_clk = 0;
    fds[fd].dbg_rx_total = 0;
    fds[fd].dbg_tx_total = 0;
    fds[fd].dbg_idle_poll_streak = 0;
    fds[fd].dbg_cts_assert_count = 0;
    fds[fd].dbg_cts_deassert_count = 0;
    fds[fd].dbg_tx_queue_full_count = 0;
    fds[fd].dbg_xon_sent_count = 0;
    fds[fd].dbg_xoff_sent_count = 0;
    fds[fd].dbg_xon_received_count = 0;
    fds[fd].dbg_xoff_received_count = 0;
    fds[fd].dbg_flow_gate_count = 0;
}

#if BMC64_RS232_LOG_LEVEL >= BMC64_LOG_TRACE
static char rs232net_dbg_hex(uint8_t v)
{
    v &= 0xf;
    return v < 10 ? (char)('0' + v) : (char)('a' + v - 10);
}
#endif

void rs232net_dbg_dump_sample(const char *label, int fd,
                              const uint8_t *data, size_t len)
{
#if BMC64_RS232_LOG_LEVEL < BMC64_LOG_TRACE
    (void)label;
    (void)fd;
    (void)data;
    (void)len;
#else
    char hex[(RS232NET_DBG_DUMP_LIMIT * 3) + 1];
    char ascii[RS232NET_DBG_DUMP_LIMIT + 1];
    size_t sample_len;
    size_t i;
    size_t pos = 0;

    if (!data || !len) {
        return;
    }

    sample_len = len;
    if (sample_len > RS232NET_DBG_DUMP_LIMIT) {
        sample_len = RS232NET_DBG_DUMP_LIMIT;
    }

    for (i = 0; i < sample_len; ++i) {
        if (pos + 3 >= sizeof hex) {
            break;
        }
        hex[pos++] = rs232net_dbg_hex(data[i] >> 4);
        hex[pos++] = rs232net_dbg_hex(data[i]);
        if (i + 1 < sample_len) {
            hex[pos++] = ' ';
        }
        ascii[i] = rs232net_dbg_chr(data[i]);
    }
    hex[pos] = '\0';
    ascii[sample_len] = '\0';

    BMC64_RS232_TRACE("%s fd %d sample %lu/%lu hex %s ascii \"%s\"",
                      label ? label : "bytes", fd,
                      (unsigned long)sample_len, (unsigned long)len, hex,
                      ascii);
#endif
}

#endif

#endif
