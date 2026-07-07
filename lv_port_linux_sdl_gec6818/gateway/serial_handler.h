#ifndef __SERIAL_HANDLER_H
#define __SERIAL_HANDLER_H
int  serial_init(const char *dev, int baud);
int  serial_send_cmd(const char *cmd);
void serial_close(void);
void *serial_rx_thread(void *arg);
#endif
