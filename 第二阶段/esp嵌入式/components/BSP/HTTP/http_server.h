#ifndef __HTTP_SERVER_H_
#define __HTTP_SERVER_H_

/* TASK4：板载 HTTP 服务器 + 内嵌点灯网页（浏览器访问 http://<开发板IP>/）
 *
 *   GET  /                    返回内嵌的点灯网页
 *   GET  /api/led             查询当前状态（JSON）
 *   POST /api/led             {"brightness":0~100} 设置亮度
 *   POST /api/led/toggle      翻转开关
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t http_server_start(uint16_t port);
esp_err_t http_server_stop(void);
bool      http_server_is_running(void);
uint16_t  http_server_get_port(void);

#ifdef __cplusplus
}
#endif

#endif
