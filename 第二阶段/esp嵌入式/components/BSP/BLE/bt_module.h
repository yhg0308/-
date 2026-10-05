#ifndef __BT_MODULE_H_
#define __BT_MODULE_H_

/* TASK4：外接 HC-04D 双模蓝牙模块（UART1 透传 + AT 配置）
 *
 *   HC-04D TXD -> ESP32 RXD (GPIO18)
 *   HC-04D RXD -> ESP32 TXD (GPIO17)
 *   VCC -> 3.3V/5V，GND 与开发板共地
 *
 * 开发板只通过 UART 与模块通信，模块负责无线透传，
 * 所以手机端发来的指令格式与 TASK3 串口完全一致。
 * 没接模块也能编译运行，bt_module_init() 探测不到就返回 ESP_ERR_NOT_FOUND。
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*bt_line_cb_t)(const char *line);

/* 初始化 UART1 + AT 探测：先按 menuconfig 的波特率发 AT，
 * 没回应就在常见波特率里扫一遍，找到能应答的就用它。 */
esp_err_t bt_module_init(void);
void      bt_module_deinit(void);

bool     bt_module_is_ready(void);
uint32_t bt_module_get_baud(void);

/* 透传接收任务：模块收到的无线数据按行回调给使用方 */
void bt_module_start_rx_task(bt_line_cb_t on_line);

esp_err_t bt_module_send(const char *str);

/* 发一条 AT 指令并等回应（阻塞，最长 timeout_ms） */
esp_err_t bt_module_at(const char *cmd, char *resp, size_t resp_len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
