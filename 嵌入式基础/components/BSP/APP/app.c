#include "app.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "breath.h"
#include "bt_module.h"
#include "cmd.h"
#include "encoder.h"
#include "http_server.h"
#include "led.h"
#include "motor.h"
#include "uart.h"
#include "wifi_sta.h"

static const char *TAG = "APP";

#define APP_TICK_MS             10      /* 心跳周期：闪烁 / 呼吸的时间基准 */
#define APP_TICK_TASK_STACK     3072
#define APP_TICK_TASK_PRIO      4

#define APP_CONSOLE_TASK_STACK  5120    /* 要跑 WiFi / HTTP / BLE 初始化，栈给足 */
#define APP_CONSOLE_TASK_PRIO   5

#define APP_QUEUE_LEN           8
#define APP_LINE_MAX            UART_CMD_LINE_MAX

static app_state_t s_state = APP_STATE_MENU;

typedef struct {
    char    line[APP_LINE_MAX];
    uint8_t src;
} app_msg_t;

static QueueHandle_t s_queue = NULL;

/* 闪烁（TASK1/3/4）：由 app_tick 每 10ms 推进，非阻塞 */
static uint8_t  s_blink_on = 0;
static uint32_t s_blink_period_ms = 500;
static uint32_t s_blink_acc_ms = 0;
static uint8_t  s_blink_level = 1;

/* TASK4 的 IP 只自动打印一次 */
static uint8_t s_wifi_reported = 0;

#if CONFIG_BSP_BT_ENABLE
static void app_bt_line_cb(const char *line);
#endif
static void app_task4_report_ip(void);

/* LED 输出总入口：TASK1 走真正的 GPIO 电平，其它 Task 走 LEDC 亮度 */
static void app_led_output(uint8_t brightness)
{
    if (s_state == APP_STATE_TASK1) {
        LED(brightness > 0);
    } else {
        led_set_brightness(brightness);
    }
}

/* 需要 LEDC 驱动 LED 时调用；已经是 PWM 模式就不再重复配置 */
static void app_ensure_pwm(void)
{
    if (!led_is_pwm()) {
        led_pwm_init();
    }
}

static void app_blink_stop(void)
{
    s_blink_on     = 0;
    s_blink_acc_ms = 0;
}

static void app_blink_start(uint32_t period_ms)
{
    if (period_ms < 50) {
        period_ms = 50;
    }
    if (period_ms > 10000) {
        period_ms = 10000;
    }
    breath_stop();                  /* 闪烁与呼吸互斥 */
    s_blink_on        = 1;
    s_blink_period_ms = period_ms;
    s_blink_acc_ms    = 0;
    s_blink_level     = 1;
    app_led_output(LED_BRIGHTNESS_MAX);
}

static void app_breath_start(void)
{
    app_blink_stop();
    breath_start();
}

/* 参数必须是 0~100 的整数，否则返回 -1 */
static int app_arg_pct(const char *arg)
{
    long v;

    if (!cmd_is_number(arg, &v)) {
        return -1;
    }
    if (v < 0 || v > 100) {
        return -1;
    }
    return (int)v;
}

/* ============================== 菜单 / 帮助 ============================== */

static void app_print_menu(void)
{
    uart_send("\r\n"
              "================ FinalWork  主菜单 ================\r\n"
              " 输入 Task1 ~ Task5 进入对应的演示功能：\r\n"
              "   Task1  GPIO 点灯\r\n"
              "   Task2  LEDC 呼吸灯\r\n"
              "   Task3  UART 串口协议点灯\r\n"
              "   Task4  Wi-Fi / 蓝牙无线点灯\r\n"
              "   Task5  直流电机（无级调速 / 编码器测速 / PID 闭环）\r\n"
              "   Task6  EEZ Studio + LVGL 触摸屏（本次未完成）\r\n"
              " 其它：HELP 帮助 | STATUS 状态 | INFO 硬件信息 | MENU 回主菜单\r\n"
              "==================================================\r\n");
}

static void app_print_help_task1(void)
{
    uart_send("-------- Task1  GPIO 点灯 --------\r\n"
              " ON / OFF      点亮 / 熄灭（GPIO 高、低电平）\r\n"
              " TOGGLE        翻转电平\r\n"
              " BLINK [ms]    自动闪烁，周期默认 500ms（50~10000）\r\n"
              " READ          回读引脚当前电平\r\n"
              " STOP          停止闪烁并熄灭\r\n"
              "----------------------------------\r\n");
}

static void app_print_help_task2(void)
{
    uart_send("-------- Task2  LEDC 呼吸灯 --------\r\n"
              " BREATH        开始呼吸（0 -> 100 -> 0 循环）\r\n"
              " ON / OFF      常亮 100% / 熄灭\r\n"
              " DUTY <0-100>  直接设定亮度（会结束呼吸）\r\n"
              " STEP <ms> [pct]  调整呼吸节奏，默认 20ms / 1%\r\n"
              " FREQ <hz>     重设 PWM 频率（100~20000）\r\n"
              " STOP          停止呼吸并熄灭\r\n"
              "------------------------------------\r\n");
}

static void app_print_help_task3(void)
{
    uart_send("-------- Task3  UART 串口协议点灯 --------\r\n"
              " 格式：<命令> [参数]，大小写均可，行尾 \\r 或 \\n\r\n"
              " ON             点亮（100%）\r\n"
              " OFF            熄灭（0%）\r\n"
              " SPEED <0-100>  设置亮度（越界返回 ERROR）\r\n"
              " BRIGHT <0-100> SPEED 的别名\r\n"
              " TOGGLE         翻转\r\n"
              " BLINK <ms>     以 ms 为周期闪烁（50~10000）\r\n"
              " BREATH         进入呼吸模式\r\n"
              " FREQ <hz>      重设 PWM 频率\r\n"
              " STOP           停止所有效果并熄灭\r\n"
              "------------------------------------------\r\n");
}

static void app_print_help_task4(void)
{
    uart_send("-------- Task4  无线点灯 --------\r\n"
              " WIFI           连接路由器（SSID / 密码见 menuconfig）\r\n"
              " IP             打印 IP / RSSI / 连接状态\r\n"
              " HTTP / HTTP OFF   启动 / 停止 HTTP 服务器\r\n"
              " BLE / BLE OFF     初始化 HC-04D 透传 / 关闭蓝牙串口\r\n"
              " ON / OFF / TOGGLE\r\n"
              " SPEED <0-100>  设置亮度（与 Task3 格式一致）\r\n"
              " BREATH / BLINK <ms> / STOP\r\n"
              " 网页控制：http://<开发板IP>/\r\n"
              "---------------------------------\r\n");
}

static void app_print_state_help(void)
{
    switch (s_state) {
    case APP_STATE_TASK1: app_print_help_task1(); break;
    case APP_STATE_TASK2: app_print_help_task2(); break;
    case APP_STATE_TASK3: app_print_help_task3(); break;
    case APP_STATE_TASK4: app_print_help_task4(); break;
    case APP_STATE_TASK5: cmd_print_help(); break;
    default:              app_print_menu();       break;
    }
}

const char *app_state_name(void)
{
    switch (s_state) {
    case APP_STATE_TASK1: return "TASK1";
    case APP_STATE_TASK2: return "TASK2";
    case APP_STATE_TASK3: return "TASK3";
    case APP_STATE_TASK4: return "TASK4";
    case APP_STATE_TASK5: return "TASK5";
    default:              return "MENU";
    }
}

app_state_t app_get_state(void)
{
    return s_state;
}

static void app_print_status(void)
{
    uart_printf("[STATUS] state=%s | LED=%u%% (%s) breath=%s blink=%s | motor=%s/%s duty=%u%%"
                " rpm=%.0f | wifi=%s | http=%s | bt=%s\r\n",
                app_state_name(),
                (unsigned)led_get_brightness(),
                led_is_pwm() ? "PWM" : "GPIO",
                breath_is_running() ? "ON" : "OFF",
                s_blink_on ? "ON" : "OFF",
                motor_is_running() ? "ON" : "OFF",
                motor_is_closed_loop() ? "CLOSED" : (motor_is_auto() ? "AUTO" : "OPEN"),
                (unsigned)motor_get_speed(),
                (double)motor_get_rpm(),
                wifi_sta_is_connected() ? "connected" : "off",
                http_server_is_running() ? "ON" : "OFF",
                bt_module_is_ready() ? "ON" : "OFF");

    if (s_state == APP_STATE_TASK5) {
        cmd_print_status();
    }
}

static void app_print_info(void)
{
    esp_chip_info_t chip;
    char            ip[16] = {0};

    esp_chip_info(&chip);
    wifi_sta_get_ip_str(ip, sizeof(ip));

    uart_send("\r\n=============== 硬件 / 参数信息 ===============\r\n");
    uart_printf(" 芯片           : ESP32-S3, %d core(s), rev v%d.%d\r\n",
                chip.cores, chip.revision / 100, chip.revision % 100);
    uart_printf(" ESP-IDF        : %s\r\n", IDF_VER);
    uart_printf(" Flash          : %s\r\n", CONFIG_ESPTOOLPY_FLASHSIZE);
    uart_printf(" 空闲堆         : %" PRIu32 " bytes\r\n",
                (uint32_t)esp_get_free_heap_size());
    uart_printf(" Wi-Fi IP       : %s\r\n", (ip[0] != '\0') ? ip : "(未连接)");
    uart_send(" ---- 引脚 ----\r\n");
    uart_printf(" LED            : GPIO%d\r\n", (int)LED_GPIO_PIN);
    uart_printf(" 电机 PWMA      : GPIO%d  (LEDC TIMER_1 / CHANNEL_1)\r\n",
                (int)MOTOR_PWM_GPIO);
    uart_printf(" 电机 AIN1/AIN2 : GPIO%d / GPIO%d\r\n",
                (int)MOTOR_DIR_GPIO_A, (int)MOTOR_DIR_GPIO_B);
    uart_printf(" 编码器 A/B     : GPIO%d / GPIO%d  (PCNT 4 倍频)\r\n",
                (int)ENCODER_A_GPIO, (int)ENCODER_B_GPIO);
    uart_printf(" 蓝牙 UART1     : TX=GPIO%d RX=GPIO%d\r\n",
                CONFIG_BSP_BT_TX_GPIO, CONFIG_BSP_BT_RX_GPIO);
    uart_printf(" 控制台 UART0   : TX=GPIO%d RX=GPIO%d\r\n",
                USART_TX_GPIO_PIN, USART_RX_GPIO_PIN);
    uart_send(" ---- 关键参数 ----\r\n");
    uart_printf(" LED PWM        : %" PRIu32 " Hz / %u bit (%u 档)\r\n",
                led_pwm_get_freq(), (unsigned)led_pwm_get_resolution(),
                (unsigned)(1u << led_pwm_get_resolution()));
    uart_printf(" 电机 PWM       : %d Hz / %d bit\r\n",
                MOTOR_PWM_FREQ_HZ, (int)MOTOR_PWM_RES);
    uart_printf(" 编码器         : %d 线 x %d 减速比 x %d 倍频 = %d 计数/圈\r\n",
                ENCODER_PPR, ENCODER_GEAR_RATIO, ENCODER_MULTIPLIER,
                ENCODER_COUNTS_PER_REV);
    uart_printf(" PID            : kp=%.2f ki=%.2f kd=%.2f, 周期 %d ms\r\n",
                (double)MOTOR_PID_KP, (double)MOTOR_PID_KI, (double)MOTOR_PID_KD,
                MOTOR_RAMP_PERIOD_MS * MOTOR_PID_DIV);
    uart_send(" 串口           : 115200 8N1\r\n"
              "==============================================\r\n");
}

/* ==================== Task1 ~ Task4 共用的 LED 指令表 ==================== */

typedef struct {
    uint8_t allow_breath;       /* 是否允许 BREATH / STEP / FREQ */
    uint8_t allow_speed;        /* 是否允许 SPEED / BRIGHT / DUTY */
    uint8_t allow_read;         /* 是否允许 READ（回读 GPIO 电平） */
} app_led_opts_t;

static const app_led_opts_t s_opts_task1 = { 0, 0, 1 };
static const app_led_opts_t s_opts_task2 = { 1, 1, 0 };
static const app_led_opts_t s_opts_task3 = { 1, 1, 0 };
static const app_led_opts_t s_opts_task4 = { 1, 1, 0 };

static void app_handle_led_cmd(const char *cmd, const char *arg, const app_led_opts_t *opt)
{
    if (strcmp(cmd, "ON") == 0) {
        if (arg != NULL) {
            uart_send("ERROR\r\n");
            return;
        }
        app_blink_stop();
        breath_stop();
        app_ensure_pwm();
        app_led_output(LED_BRIGHTNESS_MAX);
        uart_printf("OK: LED ON (%s)\r\n",
                    (s_state == APP_STATE_TASK1) ? "GPIO high" : "duty 100%");
        return;
    }

    if (strcmp(cmd, "OFF") == 0) {
        if (arg != NULL) {
            uart_send("ERROR\r\n");
            return;
        }
        app_blink_stop();
        breath_stop();
        app_led_output(0);
        uart_printf("OK: LED OFF (%s)\r\n",
                    (s_state == APP_STATE_TASK1) ? "GPIO low" : "duty 0%");
        return;
    }

    if (strcmp(cmd, "TOGGLE") == 0) {
        if (arg != NULL) {
            uart_send("ERROR\r\n");
            return;
        }
        app_blink_stop();
        breath_stop();
        if (s_state == APP_STATE_TASK1) {
            LED_TOGGLE();
        } else {
            app_ensure_pwm();
            led_toggle();
        }
        uart_printf("OK: LED %s\r\n", led_is_on() ? "ON" : "OFF");
        return;
    }

    if (strcmp(cmd, "SPEED") == 0 || strcmp(cmd, "BRIGHT") == 0 || strcmp(cmd, "DUTY") == 0) {
        int pct;

        if (!opt->allow_speed) {
            uart_send("ERROR\r\n");       /* Task1 是纯 GPIO 点灯，没有亮度 */
            return;
        }
        pct = app_arg_pct(arg);
        if (pct < 0) {
            uart_send("ERROR\r\n");       /* 缺参数 / 非数字 / 越界 */
            return;
        }
        app_blink_stop();
        breath_stop();
        app_ensure_pwm();
        led_set_brightness((uint8_t)pct);
        uart_printf("OK: brightness=%d%%\r\n", pct);
        return;
    }

    if (strcmp(cmd, "BLINK") == 0) {
        uint32_t period = 500;

        if (arg != NULL) {
            long v;
            if (!cmd_is_number(arg, &v) || v < 50 || v > 10000) {
                uart_send("ERROR\r\n");
                return;
            }
            period = (uint32_t)v;
        }
        app_ensure_pwm();
        app_blink_start(period);
        uart_printf("OK: blink %" PRIu32 " ms\r\n", period);
        return;
    }

    if (strcmp(cmd, "BREATH") == 0) {
        if (arg != NULL || !opt->allow_breath) {
            uart_send("ERROR\r\n");
            return;
        }
        app_ensure_pwm();
        app_breath_start();
        uart_send("OK: breath ON\r\n");
        return;
    }

    if (strcmp(cmd, "STEP") == 0) {
        /* STEP <tick_ms> [step_pct] */
        char  tmp[APP_LINE_MAX];
        long  tick;
        long  pct = BREATH_STEP_PCT;
        char *sp;

        if (!opt->allow_breath || arg == NULL || strlen(arg) >= sizeof(tmp)) {
            uart_send("ERROR\r\n");
            return;
        }
        strncpy(tmp, arg, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';

        sp = strchr(tmp, ' ');
        if (sp != NULL) {
            *sp++ = '\0';
            if (!cmd_is_number(sp, &pct)) {
                uart_send("ERROR\r\n");
                return;
            }
        }
        if (!cmd_is_number(tmp, &tick) || tick < 5 || tick > 500 || pct < 1 || pct > 20) {
            uart_send("ERROR\r\n");
            return;
        }
        breath_set_step((uint32_t)tick, (uint8_t)pct);
        uart_printf("OK: breath step %ld ms / %ld%%\r\n", tick, pct);
        return;
    }

    if (strcmp(cmd, "FREQ") == 0) {
        long hz;

        if (!opt->allow_speed) {
            uart_send("ERROR\r\n");
            return;
        }
        if (!cmd_is_number(arg, &hz) || hz < 100 || hz > 20000) {
            uart_send("ERROR\r\n");
            return;
        }
        app_ensure_pwm();
        led_pwm_set_freq((uint32_t)hz);
        uart_printf("OK: PWM %ld Hz / %u bit (%u 档)\r\n", hz,
                    (unsigned)led_pwm_get_resolution(),
                    (unsigned)(1u << led_pwm_get_resolution()));
        return;
    }

    if (strcmp(cmd, "READ") == 0) {
        if (arg != NULL || !opt->allow_read) {
            uart_send("ERROR\r\n");
            return;
        }
        uart_printf("[GPIO] level=%d\r\n", led_gpio_read() ? 1 : 0);
        return;
    }

    if (strcmp(cmd, "STOP") == 0) {
        if (arg != NULL) {
            uart_send("ERROR\r\n");
            return;
        }
        app_blink_stop();
        breath_stop();
        app_led_output(0);
        uart_send("OK: all LED effects stopped\r\n");
        return;
    }

    uart_send("ERROR\r\n");
}

/* ============================ Task4 专有指令 ============================ */

static void app_task4_report_ip(void)
{
    char     ip[16] = {0};
    uint32_t rssi   = wifi_sta_get_rssi();

    wifi_sta_get_ip_str(ip, sizeof(ip));
    if (ip[0] != '\0') {
        uart_printf("[WIFI] 已连接: IP=%s RSSI=%d dBm  ->  浏览器打开 http://%s/\r\n",
                    ip, (int)(int8_t)(rssi & 0xFF), ip);
    } else {
        uart_send("[WIFI] 链路已建立但还没拿到 IP\r\n");
    }
}

static void app_task4_start(void)
{
    s_wifi_reported = 0;

    if (wifi_sta_init() != ESP_OK) {
        uart_send("ERROR: Wi-Fi 初始化失败\r\n");
    } else {
        uart_send("OK: Wi-Fi STA 启动，正在连接路由器（SSID 见 menuconfig）...\r\n");
    }

    if (http_server_start(CONFIG_BSP_HTTP_PORT) == ESP_OK) {
        uart_printf("OK: HTTP 服务器已启动，端口 %u\r\n",
                    (unsigned)http_server_get_port());
    } else {
        uart_send("ERROR: HTTP 服务器启动失败\r\n");
    }

#if CONFIG_BSP_BT_ENABLE
    uart_send("OK: 正在探测外接蓝牙模块 HC-04D（没接模块会等约 2 秒）...\r\n");
    if (bt_module_init() == ESP_OK) {
        bt_module_start_rx_task(app_bt_line_cb);
        uart_printf("OK: HC-04D 在线 (baud=%" PRIu32 ")，蓝牙指令与 Task3 完全一致\r\n",
                    (uint32_t)bt_module_get_baud());
    } else {
        uart_send("提示: 未检测到 HC-04D，蓝牙功能暂不可用（Wi-Fi 点灯不受影响）\r\n");
    }
#else
    uart_send("提示: menuconfig 里关闭了 CONFIG_BSP_BT_ENABLE，跳过蓝牙初始化\r\n");
#endif
}

static void app_handle_task4_cmd(const char *cmd, const char *arg)
{
    if (strcmp(cmd, "WIFI") == 0) {
        if (arg != NULL) {
            uart_send("ERROR\r\n");
            return;
        }
        if (wifi_sta_init() != ESP_OK) {
            uart_send("ERROR: Wi-Fi 初始化失败\r\n");
            return;
        }
        uart_send("OK: Wi-Fi STA 已启动\r\n");
        return;
    }

    if (strcmp(cmd, "IP") == 0) {
        if (arg != NULL) {
            uart_send("ERROR\r\n");
            return;
        }
        app_task4_report_ip();
        return;
    }

    if (strcmp(cmd, "HTTP") == 0) {
        if (arg != NULL) {
            if (strcmp(arg, "OFF") != 0) {
                uart_send("ERROR\r\n");
                return;
            }
            http_server_stop();
            uart_send("OK: HTTP 服务器已停止\r\n");
            return;
        }
        if (http_server_start(CONFIG_BSP_HTTP_PORT) == ESP_OK) {
            uart_printf("OK: HTTP 服务器端口 %u\r\n",
                        (unsigned)http_server_get_port());
        } else {
            uart_send("ERROR: 启动失败\r\n");
        }
        return;
    }

    if (strcmp(cmd, "BLE") == 0) {
        if (arg != NULL) {
            if (strcmp(arg, "OFF") != 0) {
                uart_send("ERROR\r\n");
                return;
            }
            bt_module_deinit();
            uart_send("OK: 蓝牙串口已关闭\r\n");
            return;
        }
#if CONFIG_BSP_BT_ENABLE
        if (bt_module_init() == ESP_OK) {
            bt_module_start_rx_task(app_bt_line_cb);
            uart_printf("OK: HC-04D 在线 (baud=%" PRIu32 ")，"
                        "手机用\"蓝牙串口 - 江协科技\"小程序连接即可发指令\r\n",
                        (uint32_t)bt_module_get_baud());
        } else {
            uart_send("ERROR: 未检测到 HC-04D（检查供电 / 共地 / TX-RX 交叉 / 波特率）\r\n");
        }
#else
        uart_send("ERROR: menuconfig 里关闭了 CONFIG_BSP_BT_ENABLE\r\n");
#endif
        return;
    }

    app_handle_led_cmd(cmd, arg, &s_opts_task4);
}

/* ============================== 状态切换 ============================== */

static void app_enter_state(app_state_t next)
{
    if (next == s_state) {
        app_print_state_help();         /* 重复输入同一个 Task：再给一遍帮助 */
        return;
    }

    /* ---- 退出旧状态 ---- */
    switch (s_state) {
    case APP_STATE_TASK1:
    case APP_STATE_TASK2:
    case APP_STATE_TASK3:
    case APP_STATE_TASK4:
        app_blink_stop();
        breath_stop();
        break;
    default:
        break;                          /* TASK5 保持电机原状，让用户自己发 OFF */
    }

    s_state = next;

    /* ---- 进入新状态 ---- */
    switch (next) {
    case APP_STATE_MENU:
        app_ensure_pwm();
        led_set_brightness(0);
        app_print_menu();
        break;

    case APP_STATE_TASK1:
        /* TASK1 的重点是真正的 GPIO 点灯：把引脚从 LEDC 手里收回来 */
        led_gpio_init();
        LED(1);
        uart_send("\r\n[Task1] GPIO 点灯：LED 已点亮（GPIO1 输出高电平）\r\n");
        uart_printf("[Task1] 实测引脚电平 = %d（随时可用 READ 回读）\r\n",
                    led_gpio_read() ? 1 : 0);
        app_blink_start(500);
        uart_send("[Task1] 已自动开始 500ms 闪烁；ON / OFF 可改为常亮 / 常灭\r\n");
        app_print_help_task1();
        break;

    case APP_STATE_TASK2:
        led_pwm_init();
        uart_send("\r\n[Task2] LEDC 呼吸灯：亮度 0 -> 100 -> 0 平滑循环\r\n");
        app_breath_start();
        uart_printf("[Task2] PWM %" PRIu32 " Hz / %u bit -> 占空比 %u 档\r\n",
                    led_pwm_get_freq(), (unsigned)led_pwm_get_resolution(),
                    (unsigned)(1u << led_pwm_get_resolution()));
        app_print_help_task2();
        break;

    case APP_STATE_TASK3:
        app_ensure_pwm();
        led_set_brightness(0);
        uart_send("\r\n[Task3] UART 串口协议点灯：115200 8N1，中断接收 + 环形缓冲\r\n");
        uart_send("[Task3] 直接输入 ON / OFF / SPEED 50 / BREATH / BLINK 200 等指令\r\n");
        app_print_help_task3();
        break;

    case APP_STATE_TASK4:
        app_ensure_pwm();
        led_set_brightness(0);
        uart_send("\r\n[Task4] 无线点灯：Wi-Fi STA + HTTP 网页 + 外接 HC-04D 蓝牙透传\r\n");
        app_task4_start();
        app_print_help_task4();
        break;

    case APP_STATE_TASK5:
        uart_send("\r\n[Task5] 直流电机控制（TB6612 + LEDC 20kHz / 10bit）\r\n");
        uart_printf("[Task5] PWMA=GPIO%d AIN1=GPIO%d AIN2=GPIO%d\r\n",
                    (int)MOTOR_PWM_GPIO, (int)MOTOR_DIR_GPIO_A, (int)MOTOR_DIR_GPIO_B);
        uart_printf("[Task5] 编码器 A=GPIO%d B=GPIO%d  %d 线 x %d 减速比 x %d 倍频"
                    " = %d 计数/圈\r\n",
                    (int)ENCODER_A_GPIO, (int)ENCODER_B_GPIO,
                    ENCODER_PPR, ENCODER_GEAR_RATIO, ENCODER_MULTIPLIER,
                    ENCODER_COUNTS_PER_REV);
        uart_printf("[Task5] PID kp=%.2f ki=%.2f kd=%.2f, 周期 %d ms\r\n",
                    (double)MOTOR_PID_KP, (double)MOTOR_PID_KI, (double)MOTOR_PID_KD,
                    MOTOR_RAMP_PERIOD_MS * MOTOR_PID_DIV);
        app_print_state_help();
        break;

    default:
        break;
    }
}

/* ============================== 指令分发 ============================== */

static void app_dispatch(char *line)
{
    char  full[APP_LINE_MAX];
    char *arg = NULL;
    char *cmd;

    /* cmd_split 会就地修改 line，TASK5 的指令表需要完整的一行，先留一份 */
    strncpy(full, line, sizeof(full) - 1);
    full[sizeof(full) - 1] = '\0';

    cmd = cmd_split(line, &arg);
    if (cmd == NULL || cmd[0] == '\0') {
        return;                     /* 空行静默忽略 */
    }

    /* ---- 全局指令（任何状态都认） ---- */
    if (strcmp(cmd, "TASK1") == 0 || strcmp(cmd, "T1") == 0) {
        app_enter_state(APP_STATE_TASK1);
        return;
    }
    if (strcmp(cmd, "TASK2") == 0 || strcmp(cmd, "T2") == 0) {
        app_enter_state(APP_STATE_TASK2);
        return;
    }
    if (strcmp(cmd, "TASK3") == 0 || strcmp(cmd, "T3") == 0) {
        app_enter_state(APP_STATE_TASK3);
        return;
    }
    if (strcmp(cmd, "TASK4") == 0 || strcmp(cmd, "T4") == 0) {
        app_enter_state(APP_STATE_TASK4);
        return;
    }
    if (strcmp(cmd, "TASK5") == 0 || strcmp(cmd, "T5") == 0) {
        app_enter_state(APP_STATE_TASK5);
        return;
    }
    if (strcmp(cmd, "TASK6") == 0 || strcmp(cmd, "T6") == 0) {
        uart_send("提示: Task6（EEZ Studio + LVGL 触摸屏）本次未完成，"
                  "详见 README 的\"任务完成情况\"\r\n");
        return;
    }

    if (strcmp(cmd, "MENU") == 0 || strcmp(cmd, "BACK") == 0 || strcmp(cmd, "EXIT") == 0) {
        app_enter_state(APP_STATE_MENU);
        return;
    }
    if (strcmp(cmd, "HELP") == 0 || strcmp(cmd, "?") == 0) {
        app_print_state_help();
        return;
    }
    if (strcmp(cmd, "STATUS") == 0) {
        app_print_status();
        return;
    }
    if (strcmp(cmd, "INFO") == 0) {
        app_print_info();
        return;
    }

    /* ---- 各状态自己的指令表 ---- */
    switch (s_state) {
    case APP_STATE_TASK1:
        app_handle_led_cmd(cmd, arg, &s_opts_task1);
        break;
    case APP_STATE_TASK2:
        app_handle_led_cmd(cmd, arg, &s_opts_task2);
        break;
    case APP_STATE_TASK3:
        app_handle_led_cmd(cmd, arg, &s_opts_task3);
        break;
    case APP_STATE_TASK4:
        app_handle_task4_cmd(cmd, arg);
        break;
    case APP_STATE_TASK5:
        cmd_handle_line(full);      /* TASK5 的指令表在 CMD 组件里 */
        break;
    default:
        uart_send("ERROR: 请先输入 Task1 ~ Task5 选择演示功能（HELP 看菜单）\r\n");
        break;
    }
}

void app_handle_line(const char *line, app_src_t src)
{
    char buf[APP_LINE_MAX];

    if (line == NULL) {
        return;
    }
    if (strlen(line) >= sizeof(buf)) {
        uart_send("ERROR: line too long\r\n");
        return;
    }
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* 蓝牙透传进来的指令先回显一行，方便对照手机端发了什么 */
    if (src == APP_SRC_BT) {
        uart_printf("[BLE] > %s\r\n", buf);
    }

    app_dispatch(buf);
}

/* ================================ 任务 ================================ */

static void app_console_task(void *arg)
{
    app_msg_t msg;

    (void)arg;

    while (1) {
        if (xQueueReceive(s_queue, &msg, portMAX_DELAY) == pdTRUE) {
            app_handle_line(msg.line, (app_src_t)msg.src);
        }
    }
}

static void app_tick(void)
{
    if (s_blink_on) {
        s_blink_acc_ms += APP_TICK_MS;
        if (s_blink_acc_ms >= s_blink_period_ms) {
            s_blink_acc_ms = 0;
            s_blink_level  = s_blink_level ? 0 : 1;
            app_led_output(s_blink_level ? LED_BRIGHTNESS_MAX : 0);
        }
    }

    if (breath_is_running()) {
        breath_tick(APP_TICK_MS);
    }

    /* TASK4：Wi-Fi 拿到 IP 后自动打印一次访问地址 */
    if (s_state == APP_STATE_TASK4 && !s_wifi_reported && wifi_sta_is_connected()) {
        s_wifi_reported = 1;
        app_task4_report_ip();
    }
}

static void app_tick_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();

    (void)arg;

    while (1) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(APP_TICK_MS));
        app_tick();
    }
}

/* 接收回调在 UART / BLE 的接收任务上下文里执行，只允许入队 */
static void app_enqueue(const char *line, app_src_t src)
{
    app_msg_t msg;

    if (line == NULL || line[0] == '\0') {
        return;
    }

    strncpy(msg.line, line, sizeof(msg.line) - 1);
    msg.line[sizeof(msg.line) - 1] = '\0';
    msg.src = (uint8_t)src;

    if (xQueueSend(s_queue, &msg, 0) != pdTRUE) {
        /* 队列满（解析被慢动作堵住）就丢弃并提示，绝不在接收任务里阻塞 */
        uart_send("ERROR: console busy, try again\r\n");
    }
}

static void app_uart0_line_cb(const char *line)
{
    app_enqueue(line, APP_SRC_UART0);
}

#if CONFIG_BSP_BT_ENABLE
static void app_bt_line_cb(const char *line)
{
    app_enqueue(line, APP_SRC_BT);
}
#endif

void app_post_line(const char *line, app_src_t src)
{
    app_enqueue(line, src);
}

/* ================================ 启动 ================================ */

void app_start(void)
{
    usart_init(115200);

    uart_send("\r\n\r\n");
    uart_send("######################################################\r\n");
    uart_send("#  FinalWork —— 嵌入式基础（方向二：ESP-IDF）整合版  #\r\n");
    uart_send("#  FOCUS 2026 秋季招新控制组考核 · 阶段二              #\r\n");
    uart_send("######################################################\r\n");
    uart_printf("#  ESP-IDF %s | ESP32-S3 | 串口 115200 8N1\r\n", IDF_VER);

    s_queue = xQueueCreate(APP_QUEUE_LEN, sizeof(app_msg_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "指令队列创建失败");
        return;
    }
    uart_start_rx_task(app_uart0_line_cb);

    led_init();
    encoder_init();
    motor_init();

    xTaskCreate(app_console_task, "app_console", APP_CONSOLE_TASK_STACK, NULL,
                APP_CONSOLE_TASK_PRIO, NULL);
    xTaskCreate(app_tick_task, "app_tick", APP_TICK_TASK_STACK, NULL,
                APP_TICK_TASK_PRIO, NULL);
    xTaskCreate(motor_ramp_task, "motor_ramp", 4096, NULL, 5, NULL);

    app_print_menu();
    uart_send("本次已完成 TASK1~TASK5（含 TASK5 拓展），TASK6 未完成。\r\n");
    uart_send("输入 Task1 开始演示；输入 INFO 可查看引脚与关键参数。\r\n");

    ESP_LOGI(TAG, "启动完成：state=%s, 空闲堆=%u bytes",
             app_state_name(), (unsigned)esp_get_free_heap_size());
}
