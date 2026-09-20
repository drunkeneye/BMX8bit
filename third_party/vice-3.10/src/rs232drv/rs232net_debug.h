/*
 * rs232net_debug.h - private RS232 network debug helpers.
 */

#ifndef VICE_RS232NET_DEBUG_H
#define VICE_RS232NET_DEBUG_H

#include <stddef.h>
#include <stdint.h>

#include "rs232.h"

#ifdef BMC64_DEBUG_PROFILE

extern unsigned int rs232net_dbg_tx_count;
extern unsigned int rs232net_dbg_rx_count;
extern unsigned int rs232net_dbg_poll_count;
extern unsigned int rs232net_dbg_status_count;
extern unsigned int rs232net_dbg_rx_fill_count;
extern unsigned int rs232net_dbg_rx_drop_count;
extern unsigned int rs232net_dbg_rx_wait_count;

int rs232net_dbg_should_log(unsigned int count);
int rs232net_dbg_should_log_status(int fd, enum rs232handshake_out status,
                                   unsigned int count);
char rs232net_dbg_chr(uint8_t b);
void rs232net_dbg_reset_fd(int fd);
void rs232net_dbg_dump_sample(const char *label, int fd,
                              const uint8_t *data, size_t len);

#endif

#endif
