/*
 * rs232net_internal.h - private RS232 network backend interfaces.
 */

#ifndef VICE_RS232NET_INTERNAL_H
#define VICE_RS232NET_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "bmc64_async_network.h"
#include "log.h"
#include "rs232.h"
#include "tcpser/modem_core.h"
#include "types.h"
#include "vicesocket.h"

#ifdef DEBUG
# define DEBUG_LOG_MESSAGE(_xxx) log_message _xxx
#else
# define DEBUG_LOG_MESSAGE(_xxx)
#endif

#define RS232NET_HAYES_AUDIO_DIAL 1
#define RS232NET_HAYES_AUDIO_SHORT 2
#define RS232NET_HAYES_AUDIO_LONG 3

#define RS232NET_RXBUF_SIZE 4096
#define RS232NET_RXBUF_REFILL_THRESHOLD 256
/*
 * Circle sockets can drop data if Receive() is called with a buffer smaller
 * than FRAME_BUFFER_SIZE. Keep this in sync with circle/netdevice.h.
 */
#define RS232NET_CIRCLE_FRAME_BUFFER_SIZE 1600
#define RS232NET_RXBUF_READ_CHUNK RS232NET_CIRCLE_FRAME_BUFFER_SIZE
#define RS232NET_RX_FLOW_DEASSERT_FREE RS232NET_CIRCLE_FRAME_BUFFER_SIZE
#define RS232NET_RX_FLOW_ASSERT_FREE (RS232NET_CIRCLE_FRAME_BUFFER_SIZE * 2)
#define RS232NET_TX_FLOW_DEASSERT_FREE 512
#define RS232NET_TX_FLOW_ASSERT_FREE 2048
#define RS232NET_XON 0x11
#define RS232NET_XOFF 0x13

typedef struct rs232net {
    int inuse;
    vice_network_socket_t *fd;
    int device;
    int deferred;
    int useip232;
    int dcd_in;
    int ri_in;
    int dtr_out;
    int rts_out;
    int cts_in;
    int cts_backpressure;
    int rx_flow_paused;
    int tx_flow_paused;
    int flow_control_opts;
    int xonxoff_dte_paused;
    int xonxoff_xoff_sent;
    int hayes;
    int rx_disconnect_pending;
    int modem_connect_pending;
#ifdef RASPI_COMPILE
    bmc64_async_net_handle_t *async;
#endif
    modem_config modem;
    uint8_t rxbuf[RS232NET_RXBUF_SIZE];
    size_t rxbuf_pos;
    size_t rxbuf_len;
#ifdef BMC64_DEBUG_PROFILE
    unsigned long dbg_last_rx_fill_clk;
    unsigned long dbg_last_tx_clk;
    unsigned long dbg_last_idle_report_clk;
    unsigned long dbg_rx_total;
    unsigned long dbg_tx_total;
    unsigned int dbg_idle_poll_streak;
    unsigned int dbg_cts_assert_count;
    unsigned int dbg_cts_deassert_count;
    unsigned int dbg_tx_queue_full_count;
    unsigned int dbg_xon_sent_count;
    unsigned int dbg_xoff_sent_count;
    unsigned int dbg_xon_received_count;
    unsigned int dbg_xoff_received_count;
    unsigned int dbg_flow_gate_count;
#endif
} rs232net_t;

extern rs232net_t fds[RS232_NUM_DEVICES];
extern log_t rs232net_log;
extern int rs232net_hayes_mode;
extern int rs232net_hayes_audio_mode;

extern unsigned long circle_get_ticks(void);
#ifdef RASPI_COMPILE
extern int emux_network_is_ready(void);
extern void bmc64_modem_audio_set_mode(int mode);
extern void bmc64_modem_audio_play_dial_blocking(void);
extern void bmc64_modem_audio_play_connect_blocking(void);
extern void bmc64_modem_audio_play_connect_async(void);
extern void bmc64_modem_audio_stop(void);
#endif

void rs232net_clear_rxbuf(int fd);
size_t rs232net_rxbuf_pending(int fd);
size_t rs232net_rxbuf_free(int fd);
void rs232net_compact_rxbuf(int fd);
int rs232net_append_rxbuf(int fd, const uint8_t *data, size_t len);
int rs232net_fill_rxbuf_from_socket(int fd, const char *tag);

void rs232net_flow_reset(int fd);
void rs232net_flow_set_options(int fd, int opts);
int rs232net_flow_update(int fd, const char *tag);
int rs232net_flow_cts_active(int fd);
void rs232net_flow_note_tx_queue_full(int fd);
int rs232net_flow_dce_to_dte_paused(int fd, const char *tag);
int rs232net_flow_handle_dte_char(int fd, uint8_t b);

void rs232net_async_stop(int fd);
int rs232net_async_start(int fd, const char *target);
int rs232net_async_status(int fd);
int rs232net_socket_active(int index);
int rs232net_socket_connecting(int index);
void rs232net_poll_async(int fd);

void rs232net_closesocket(int index);
void rs232net_disconnect_deferred(int index, const char *reason);
void rs232net_mark_disconnect_pending(int index, const char *reason);
void rs232net_complete_pending_disconnect(int index);
int rs232net_link_active(int index);
int rs232net_ensure_connected(int fd);
int rs232net_connect_target(int fd, const char *target);
int rs232net_raw_putc(int fd, uint8_t b);
int rs232net_raw_getc(int fd, uint8_t *b);

void rs232net_hayes_init_fd(int fd);
void rs232net_modem_connect_succeeded(int fd);
void rs232net_modem_connect_failed(int fd);
int rs232net_hayes_putc(int fd, uint8_t b);
int rs232net_hayes_getc(int fd, uint8_t *b);
int rs232net_hayes_set_status(int fd, enum rs232handshake_out status);
enum rs232handshake_in rs232net_hayes_get_status(int fd);

int rs232net_raw_set_status(int fd, enum rs232handshake_out status);
enum rs232handshake_in rs232net_raw_get_status(int fd);

int rs232net_fd_is_valid(int fd);
int rs232net_fd_is_open(int fd);
int rs232net_fd_is_hayes(int fd);

#endif
