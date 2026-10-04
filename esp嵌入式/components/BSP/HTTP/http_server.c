#include "http_server.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "breath.h"
#include "led.h"
#include "wifi_sta.h"
#include "sdkconfig.h"

static const char *TAG = "HTTP";

static httpd_handle_t s_server = NULL;
static uint16_t        s_port   = 0;

/* CMakeLists.txt 里用 EMBED_FILES 把 led.html 编进固件，链接器生成这两个符号 */
extern const uint8_t led_html_start[] asm("_binary_led_html_start");
extern const uint8_t led_html_end[]   asm("_binary_led_html_end");

/* 拼一段 JSON 状态 */
static void make_state_json(char *out, size_t out_len)
{
    char     ip[16] = {0};
    uint32_t rssi   = wifi_sta_get_rssi();

    wifi_sta_get_ip_str(ip, sizeof(ip));

    snprintf(out, out_len,
             "{\"on\":%s,\"brightness\":%u,\"ip\":\"%s\",\"rssi\":%d,\"connected\":%s}",
             led_is_on() ? "true" : "false",
             (unsigned)led_get_brightness(),
             ip,
             (int)(int8_t)(rssi & 0xFF),
             wifi_sta_is_connected() ? "true" : "false");
}

/* GET / —— 返回点灯网页 */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    const size_t len = (size_t)(led_html_end - led_html_start);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)led_html_start, len);
}

/* GET /api/led —— 查询状态 */
static esp_err_t api_status_get_handler(httpd_req_t *req)
{
    char json[192];
    make_state_json(json, sizeof(json));

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

/* 从请求体里取一个整数（不用 cJSON，手写一个最小解析）：
 * 找 key 后面紧跟的十进制数字，思路和 TASK3 的串口指令解析一样 */
static bool body_get_int(httpd_req_t *req, const char *key, int *out)
{
    char  buf[128];
    int   total = req->content_len;
    int   received;
    const char *p;
    char *end = NULL;
    long  v;

    if (total <= 0 || total >= (int)sizeof(buf)) {
        return false;
    }

    received = httpd_req_recv(req, buf, total);
    if (received <= 0) {
        return false;
    }
    buf[received] = '\0';

    p = strstr(buf, key);
    if (p == NULL) {
        return false;
    }
    p += strlen(key);

    while (*p == '"' || *p == ':' || *p == ' ') {
        p++;
    }

    v = strtol(p, &end, 10);
    if (end == p) {
        return false;   /* 后面不是数字 */
    }

    *out = (int)v;
    return true;
}

/* 丢掉没读完的请求体，否则同一连接上的下一个请求会被当成本次的 body */
static void recv_and_discard(httpd_req_t *req)
{
    char scratch[64];
    int  remaining = req->content_len;

    while (remaining > 0) {
        int r = httpd_req_recv(req, scratch, sizeof(scratch));
        if (r <= 0) {
            break;
        }
        remaining -= r;
    }
}

/* POST /api/led —— {"brightness": 0~100}
 * 手动调光时先停掉呼吸灯，否则心跳任务每 20ms 就把亮度改回呼吸曲线上的值 */
static esp_err_t api_led_post_handler(httpd_req_t *req)
{
    int value = -1;
    char json[192];

    if (!body_get_int(req, "brightness", &value)) {
        recv_and_discard(req);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req,
                                  "{\"ok\":false,\"error\":\"need {\\\"brightness\\\":0..100}\"}");
    }

    if (value < 0)   value = 0;
    if (value > 100) value = 100;

    breath_stop();
    led_set_brightness((uint8_t)value);
    ESP_LOGI(TAG, "POST /api/led -> brightness=%d%%", value);

    make_state_json(json, sizeof(json));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

/* POST /api/led/toggle —— 翻转 */
static esp_err_t api_led_toggle_post_handler(httpd_req_t *req)
{
    char json[192];

    recv_and_discard(req);

    breath_stop();
    led_toggle();
    ESP_LOGI(TAG, "POST /api/led/toggle -> %s", led_is_on() ? "ON" : "OFF");

    make_state_json(json, sizeof(json));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static const httpd_uri_t s_uris[] = {
    { .uri = "/",               .method = HTTP_GET,  .handler = root_get_handler },
    { .uri = "/api/led",        .method = HTTP_GET,  .handler = api_status_get_handler },
    { .uri = "/api/led",        .method = HTTP_POST, .handler = api_led_post_handler },
    { .uri = "/api/led/toggle", .method = HTTP_POST, .handler = api_led_toggle_post_handler },
};

esp_err_t http_server_start(uint16_t port)
{
    esp_err_t ret;

    if (s_server != NULL) {
        ESP_LOGW(TAG, "服务器已在运行");
        return ESP_OK;
    }

    httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
    config.server_port      = port;
    config.ctrl_port        = (uint16_t)(port + 1);   /* 控制端口不能用同一个 */
    config.max_uri_handlers = sizeof(s_uris) / sizeof(s_uris[0]);
    config.stack_size       = 6144;   /* 处理函数 + 内嵌网页返回，栈给宽一点 */
    config.lru_purge_enable = true;

    ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start 失败: %s", esp_err_to_name(ret));
        s_server = NULL;
        return ret;
    }

    for (size_t i = 0; i < sizeof(s_uris) / sizeof(s_uris[0]); i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &s_uris[i]));
    }

    s_port = port;
    ESP_LOGI(TAG, "HTTP 服务器已启动，端口 %u（网页大小 %u 字节）",
             (unsigned)port, (unsigned)(led_html_end - led_html_start));
    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    esp_err_t ret;

    if (s_server == NULL) {
        return ESP_OK;
    }
    ret      = httpd_stop(s_server);
    s_server = NULL;
    s_port   = 0;
    return ret;
}

bool http_server_is_running(void)
{
    return s_server != NULL;
}

uint16_t http_server_get_port(void)
{
    return s_port;
}
