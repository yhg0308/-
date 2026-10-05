#ifndef __UART_H_
#define __UART_H_

/* TASK3 串口：115200 8N1、中断接收 + 驱动环形缓冲、按行回调（不轮询） */

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"

/* UART0 就是 ESP-IDF 的控制台串口，一根 Type-C 线既看日志又发指令 */
#define USART_UX UART_NUM_0
/* ESP-IDF 的 gpio_num_t 枚举只定义到 GPIO_NUM_39，S3 上 40~48 需直接用引脚编号 */
#define USART_TX_GPIO_PIN 43
#define USART_RX_GPIO_PIN 44

#define RX_BUF_SIZE 1024        /* 驱动内部 RX ring buffer */
#define UART_CMD_LINE_MAX 64    /* 单条指令最大长度 */

void usart_init(uint32_t baudrate);
void usart_deinit(void);

void uart_send(const char *str);
void uart_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

typedef void (*uart_line_cb_t)(const char *line);
void uart_start_rx_task(uart_line_cb_t on_line);

/* TASK4 的 HC-04D 挂在 UART1 上，复用同一套 8N1 配置 */
void usart_port_init(uart_port_t port, int tx_gpio, int rx_gpio, uint32_t baudrate);
void usart_port_deinit(uart_port_t port);

#endif
