#ifndef __WIFI_STA_H_
#define __WIFI_STA_H_

/* TASK4：Wi-Fi STA（Station）模式，连接路由器后由 DHCP 分配 IP
 *   ESP32-S3 (STA) <-- 无线 --> 路由器 (AP) <-- 局域网 --> 手机 / 电脑浏览器
 * SSID / 密码在 menuconfig 里配置，只支持 2.4GHz。
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化并开始连接（不阻塞，连上 / 断开都由事件回调处理） */
esp_err_t wifi_sta_init(void);

/* 阻塞等待拿到 IP（DHCP 结果），timeout_ms 传 0 表示一直等 */
bool wifi_sta_wait_ip(uint32_t timeout_ms);

bool     wifi_sta_is_connected(void);
uint32_t wifi_sta_get_rssi(void);                       /* dBm，负数的补码 */
void     wifi_sta_get_ip_str(char *buf, size_t len);    /* 形如 "192.168.1.123" */

#ifdef __cplusplus
}
#endif

#endif
