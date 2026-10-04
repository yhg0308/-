#include "bt_module.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdkconfig.h"
#include "uart.h"

static const char *TAG = "BT";

#define BT_UART_PORT        UART_NUM_1
#define BT_TX_GPIO          CONFIG_BSP_BT_TX_GPIO
#define BT_RX_GPIO          CONFIG_BSP_BT_RX_GPIO
#define BT_BAUD             CONFIG_BSP_BT_BAUD

#define BT_RX_TASK_STACK    3072
#define BT_RX_TASK_PRIO     9
#define BT_LINE_MAX         64
#define BT_AT_TIMEOUT_MS    300
#define BT_AT_BUF           128

/* 探测波特率：第一项是 menuconfig 配置值，其余是 HC-04D 常见出厂值 */
static const uint32_t s_probe_bauds[] = {
    BT_BAUD, 9600, 115200, 38400, 19200, 57600, 4800,
};

static bool         s_ready   = false;
static uint32_t     s_baud    = 0;
static bt_line_cb_t s_line_cb = NULL;
static volatile bool s_rx_run = false;

static void bt_flush_rx(void)
{
    uint8_t junk[64];

    while (uart_read_bytes(BT_UART_PORT, junk, sizeof(junk), 0) > 0) {
        /* 丢掉上电瞬间的残留字节 */
    }
}

/* 关串口前先让透传任务自己退出：直接删驱动会释放它正阻塞读的环形缓冲 */
static void bt_close_port(void)
{
    if (s_rx_run) {
        s_rx_run = false;
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    usart_port_deinit(BT_UART_PORT);
}

esp_err_t bt_module_send(const char *str)
{
    if (str == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    uart_write_bytes(BT_UART_PORT, str, strlen(str));
    return ESP_OK;
}

esp_err_t bt_module_at(const char *cmd, char *resp, size_t resp_len, uint32_t timeout_ms)
{
    char       line[BT_AT_BUF];
    uint8_t    buf[BT_AT_BUF];
    size_t     used = 0;
    TickType_t deadline;

    if (resp != NULL && resp_len > 0) {
        resp[0] = '\0';
    }
    if (cmd == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    bt_flush_rx();

    /* HC-04D 的 AT 指令以 \r\n 结束 */
    snprintf(line, sizeof(line), "%s\r\n", cmd);
    uart_write_bytes(BT_UART_PORT, line, strlen(line));

    deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while ((int32_t)(deadline - xTaskGetTickCount()) > 0) {
        int n = uart_read_bytes(BT_UART_PORT, buf, sizeof(buf) - 1, pdMS_TO_TICKS(20));
        if (n <= 0) {
            continue;
        }
        for (int i = 0; i < n && used < resp_len - 1 && used < sizeof(buf) - 1; i++) {
            char c = (char)buf[i];
            /* CR/LF/TAB 换成空格，resp 才能安全塞进一行日志 */
            if (c == '\r' || c == '\n' || c == '\t') {
                c = ' ';
            }
            if (resp != NULL) {
                resp[used] = c;
            }
            used++;
        }
    }

    if (resp != NULL && resp_len > 0) {
        resp[used < resp_len ? used : resp_len - 1] = '\0';
    }
    return (used > 0) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t bt_module_init(void)
{
    char      resp[BT_AT_BUF];
    size_t    n_bauds = sizeof(s_probe_bauds) / sizeof(s_probe_bauds[0]);
    esp_err_t ret = ESP_ERR_NOT_FOUND;

    if (s_ready) {
        return ESP_OK;              /* 重复调用安全 */
    }

    ESP_LOGI(TAG, "HC-04D: TX=GPIO%d RX=GPIO%d, 探测波特率 %u ...",
             BT_TX_GPIO, BT_RX_GPIO, (unsigned)BT_BAUD);

    for (size_t i = 0; i < n_bauds; i++) {
        uint32_t baud = s_probe_bauds[i];
        bool     dup  = false;

        for (size_t j = 0; j < i; j++) {
            if (s_probe_bauds[j] == baud) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }

        bt_close_port();
        usart_port_init(BT_UART_PORT, BT_TX_GPIO, BT_RX_GPIO, baud);
        vTaskDelay(pdMS_TO_TICKS(30));

        ret = bt_module_at("AT", resp, sizeof(resp), BT_AT_TIMEOUT_MS);
        if (ret == ESP_OK) {
            char name[BT_AT_BUF];

            s_baud = baud;
            /* 有些固件不支持 AT+NAME?，读不到就忽略 */
            if (bt_module_at("AT+NAME?", name, sizeof(name), BT_AT_TIMEOUT_MS) == ESP_OK) {
                ESP_LOGI(TAG, "模块在线: baud=%u, 回应=\"%s\" / NAME=\"%s\"",
                         (unsigned)baud, resp, name);
            } else {
                ESP_LOGI(TAG, "模块在线: baud=%u, 回应=\"%s\"", (unsigned)baud, resp);
            }
            s_ready = true;
            bt_flush_rx();
            return ESP_OK;
        }
    }

    ESP_LOGW(TAG, "没有探测到 HC-04D（检查供电/共地/TX-RX 是否交叉/波特率）");
    s_baud  = 0;
    s_ready = false;
    return ESP_ERR_NOT_FOUND;
}

void bt_module_deinit(void)
{
    bt_close_port();

    s_ready   = false;
    s_baud    = 0;
    s_line_cb = NULL;
}

bool bt_module_is_ready(void)
{
    return s_ready;
}

uint32_t bt_module_get_baud(void)
{
    return s_baud;
}

static void bt_rx_task(void *arg)
{
    uint8_t buf[64];
    char    line[BT_LINE_MAX];
    size_t  len = 0;

    (void)arg;

    while (s_rx_run) {
        int n = uart_read_bytes(BT_UART_PORT, buf, sizeof(buf), pdMS_TO_TICKS(20));
        if (n <= 0) {
            continue;
        }

        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];

            if (c == '\r' || c == '\n') {
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

            if (len < BT_LINE_MAX - 1) {
                line[len++] = c;
            } else {
                len = 0;            /* 一行太长就丢弃 */
            }
        }
    }

    vTaskDelete(NULL);
}

void bt_module_start_rx_task(bt_line_cb_t on_line)
{
    if (on_line == NULL || s_rx_run) {
        return;
    }
    s_line_cb = on_line;
    s_rx_run  = true;

    xTaskCreate(bt_rx_task, "bt_rx", BT_RX_TASK_STACK, NULL, BT_RX_TASK_PRIO, NULL);
}
