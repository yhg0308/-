#include "uart.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"

#define UART_RX_TASK_STACK   3072
#define UART_RX_TASK_PRIO    10

/* 一次 uart_read_bytes 最多等 20ms：第一个字节一到就返回，
 * 再等 20ms 把同一行剩下的字节凑齐。
 * 不能用 portMAX_DELAY —— 那是"必须凑够 length 个字节"的意思。 */
#define UART_RX_WAIT_MS      20

static uart_line_cb_t s_line_cb = NULL;

void usart_port_init(uart_port_t port, int tx_gpio, int rx_gpio, uint32_t baudrate)
{
    uart_config_t uart_config = {0};

    uart_config.baud_rate = (int)baudrate;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.rx_flow_ctrl_thresh = 122;
    uart_config.source_clk = UART_SCLK_APB;
    uart_param_config(port, &uart_config);

    uart_set_pin(port, tx_gpio, rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    /* 最后这个参数是中断分配标志，0 = 不额外指定（原来写 1 是 ESP_INTR_FLAG_IRAM，
     * 并不是"让 printf 走驱动的 TX buffer"，还会多打一条 warning）。 */
    ESP_ERROR_CHECK(uart_driver_install(port, RX_BUF_SIZE * 2, RX_BUF_SIZE * 2,
                                        0, NULL, 0));
}

void usart_port_deinit(uart_port_t port)
{
    uart_driver_delete(port);
}

void usart_init(uint32_t baudrate)
{
    usart_port_init(USART_UX, USART_TX_GPIO_PIN, USART_RX_GPIO_PIN, baudrate);
}

void usart_deinit(void)
{
    usart_port_deinit(USART_UX);
}

void uart_send(const char *str)
{
    if (str == NULL) {
        return;
    }
    uart_write_bytes(USART_UX, str, strlen(str));
}

void uart_printf(const char *fmt, ...)
{
    char line[160];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    uart_send(line);
}

static void uart_rx_task(void *arg)
{
    uint8_t  buf[64];
    char     line[UART_CMD_LINE_MAX];
    size_t   len = 0;

    (void)arg;

    while (1) {
        int n = uart_read_bytes(USART_UX, buf, sizeof(buf), pdMS_TO_TICKS(UART_RX_WAIT_MS));
        if (n <= 0) {
            continue;
        }

        for (int i = 0; i < n; i++) {
            uint8_t ch = buf[i];

            /* \r 和 \n 都当结束符，串口助手怎么配置都能用 */
            if (ch == '\r' || ch == '\n') {
                if (len == 0) {
                    continue;
                }
                line[len] = '\0';
                len = 0;
                if (s_line_cb != NULL) {
                    s_line_cb(line);
                }
                continue;
            }

            /* 退格：允许在串口助手里改错字 */
            if (ch == 0x08 || ch == 0x7F) {
                if (len > 0) {
                    len--;
                }
                continue;
            }

            if (len < UART_CMD_LINE_MAX - 1) {
                line[len++] = (char)ch;
            } else {
                len = 0;
                uart_send("ERROR: line too long\r\n");
            }
        }
    }
}

void uart_start_rx_task(uart_line_cb_t on_line)
{
    if (on_line == NULL) {
        ESP_LOGE("UART", "回调为空，接收任务不启动");
        return;
    }
    s_line_cb = on_line;

    xTaskCreate(uart_rx_task, "uart_rx", UART_RX_TASK_STACK, NULL,
                UART_RX_TASK_PRIO, NULL);
}
