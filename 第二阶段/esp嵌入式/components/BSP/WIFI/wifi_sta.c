#include "wifi_sta.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "sdkconfig.h"

#define WIFI_SSID           CONFIG_BSP_WIFI_SSID
#define WIFI_PASSWORD       CONFIG_BSP_WIFI_PASSWORD
#define WIFI_MAX_RETRY      CONFIG_BSP_WIFI_MAX_RETRY
#define WIFI_RETRY_MS       CONFIG_BSP_WIFI_RETRY_INTERVAL_MS

#define WIFI_CONNECTED_BIT  BIT0    /* 已连上 AP */
#define WIFI_GOT_IP_BIT     BIT1    /* 已通过 DHCP 拿到 IP */

static const char *TAG = "WIFI";

static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t       *s_netif  = NULL;
static int                s_retry  = 0;
static bool               s_inited = false;

/* Wi-Fi 驱动要把校准数据 / 连接信息存在 NVS 里，必须先初始化 */
static esp_err_t nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 需要重新初始化 (%s)", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA 已启动，开始连接 \"%s\" ...", WIFI_SSID);
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "已接入 AP（链路建立），等待 DHCP 分配 IP");
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            const wifi_event_sta_disconnected_t *d =
                (const wifi_event_sta_disconnected_t *)event_data;
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_GOT_IP_BIT);
            s_retry++;
            ESP_LOGW(TAG, "连接断开 (reason=%d)，第 %d/%d 次重试",
                     d ? d->reason : -1, s_retry, WIFI_MAX_RETRY);

            if (s_retry <= WIFI_MAX_RETRY) {
                esp_wifi_connect();
            } else {
                /* 超过次数：隔一会儿再来一轮，路由器重启后也能自己恢复 */
                ESP_LOGE(TAG, "%d 次重试都失败，%d ms 后重新尝试（检查 SSID/密码/是否 5GHz）",
                         WIFI_MAX_RETRY, WIFI_RETRY_MS);
                s_retry = 0;
                vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_MS));
                esp_wifi_connect();
            }
            break;
        }

        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = (const ip_event_got_ip_t *)event_data;
        s_retry = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_GOT_IP_BIT);
        ESP_LOGI(TAG, "拿到 IP: " IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "浏览器访问: http://" IPSTR "/", IP2STR(&e->ip_info.ip));
    }
}

esp_err_t wifi_sta_init(void)
{
    if (s_inited) {
        return ESP_OK;              /* 重复调用安全（反复输入 Task4 时） */
    }

    ESP_ERROR_CHECK(nvs_init());

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_netif = esp_netif_create_default_wifi_sta();
    if (s_netif == NULL) {
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* WIFI_EVENT 管链路状态，IP_EVENT 管 IP 地址 */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));

    /* threshold.authmode 保持 0：不限制加密方式，WPA/WPA2/WPA3 和开放网络都能连 */

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());     /* 触发 STA_START -> 回调里 connect */

    s_inited = true;
    ESP_LOGI(TAG, "Wi-Fi STA 初始化完成，目标 SSID: \"%s\"", WIFI_SSID);
    return ESP_OK;
}

bool wifi_sta_wait_ip(uint32_t timeout_ms)
{
    TickType_t ticks;
    EventBits_t bits;

    if (s_wifi_event_group == NULL) {
        return false;
    }

    ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_GOT_IP_BIT,
                               pdFALSE, pdTRUE, ticks);
    return (bits & WIFI_GOT_IP_BIT) != 0;
}

bool wifi_sta_is_connected(void)
{
    if (s_wifi_event_group == NULL) {
        return false;
    }
    return (xEventGroupGetBits(s_wifi_event_group) & WIFI_GOT_IP_BIT) != 0;
}

uint32_t wifi_sta_get_rssi(void)
{
    wifi_ap_record_t ap = {0};
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return 0;
    }
    return (uint32_t)ap.rssi;   /* 实际是 int8_t，用 uint32_t 返回负值的补码 */
}

void wifi_sta_get_ip_str(char *buf, size_t len)
{
    esp_netif_ip_info_t ip_info = {0};

    if (buf == NULL || len == 0) {
        return;
    }
    buf[0] = '\0';

    if (s_netif != NULL && esp_netif_get_ip_info(s_netif, &ip_info) == ESP_OK) {
        snprintf(buf, len, IPSTR, IP2STR(&ip_info.ip));
    }
}
