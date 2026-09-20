/*
 * rs232_hayes_modem.h - adapter callbacks used by tcpser glue.
 */

#ifndef VICE_RS232_HAYES_MODEM_H
#define VICE_RS232_HAYES_MODEM_H

int rs232net_modem_push_to_dte(int fd, const unsigned char *data, int len);
int rs232net_modem_line_connect(int fd, const char *target);
int rs232net_modem_line_write(int fd, const unsigned char *data, int len);
void rs232net_modem_line_disconnect(int fd);
void rs232net_modem_set_flow_control(int fd, int opts);

#endif
