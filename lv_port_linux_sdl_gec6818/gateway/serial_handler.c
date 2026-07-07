/* ================================================================
 *  serial_handler.c — 串口收发模块
 *
 *  功能: 打开 /dev/ttySAC2, 接收 STM32 JSON, 发送控制指令
 *        非阻塞读取 + 环形缓冲
 * ================================================================ */
#include "gateway.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/time.h>

static int uart_fd = -1;
extern RingBuffer g_ring;

/* 初始化串口: 115200 8N1 */
int serial_init(const char *dev, int baud)
{
    struct termios opt;

    uart_fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (uart_fd < 0) {
        fprintf(stderr, "[GW-UART] open %s failed: %s\n", dev, strerror(errno));
        return -1;
    }

    tcgetattr(uart_fd, &opt);
    cfmakeraw(&opt);
    opt.c_cflag |= CLOCAL | CREAD;
    cfsetspeed(&opt, B115200);
    opt.c_cflag &= ~CSIZE; opt.c_cflag |= CS8;
    opt.c_cflag &= ~PARENB;
    opt.c_cflag &= ~CSTOPB;
    opt.c_cc[VTIME] = 1;
    opt.c_cc[VMIN]  = 0;
    tcflush(uart_fd, TCIOFLUSH);
    tcsetattr(uart_fd, TCSANOW, &opt);

    printf("[GW-UART] %s opened %d baud\n", dev, baud);
    return 0;
}

/* 接收线程: 非阻塞读取, 按行解析 JSON */
void *serial_rx_thread(void *arg)
{
    (void)arg;
    char buf[512];
    int  pos = 0;

    while (g_running) {
        char ch;
        int n = read(uart_fd, &ch, 1);
        if (n <= 0) { usleep(5000); continue; }

        if (ch == '\n' || ch == '\r') {
            if (pos > 0) {
                buf[pos] = '\0';
                /* 解析 JSON → SensorData */
                SensorData sd;
                memset(&sd, 0, sizeof(sd));
                if (sscanf(buf,
                    "{\"temp\":%f,\"humi\":%f,\"light\":%f,"
                    "\"do\":%f,\"ph\":%f,\"turb\":%f,\"stat\":%d}",
                    &sd.water_temp, &sd.humidity, &sd.light,
                    &sd.dissolved_o2, &sd.ph, &sd.turbidity,
                    &sd.sensor_status) >= 6) {

                    /* 加时间戳 */
                    time_t now = time(NULL);
                    strftime(sd.timestamp, sizeof(sd.timestamp),
                             "%H:%M:%S", localtime(&now));

                    /* 存环形缓冲 */
                    ringbuf_put(&g_ring, &sd);
                    __sync_fetch_and_add(&g_stats.rx_count, 1);
                }
                pos = 0;
            }
        } else if (pos < (int)sizeof(buf) - 1) {
            buf[pos++] = ch;
        }
    }
    return NULL;
}

/* 发送控制指令给 STM32 */
int serial_send_cmd(const char *cmd)
{
    if (uart_fd < 0) return -1;
    int len = strlen(cmd);
    int ret = write(uart_fd, cmd, len);
    return (ret == len) ? 0 : -1;
}

void serial_close(void)
{
    if (uart_fd >= 0) { close(uart_fd); uart_fd = -1; }
}
