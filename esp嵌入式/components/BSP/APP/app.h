#ifndef __APP_H_
#define __APP_H_

/* APP —— 串口主控状态机：输入 Task1 ~ Task5 切换到对应的演示功能
 *
 *   Task1 GPIO 点灯 / Task2 LEDC 呼吸灯 / Task3 串口协议点灯
 *   Task4 Wi-Fi+蓝牙点灯 / Task5 直流电机 + 编码器 + PID
 *
 * 任务划分：uart_rx 收字节入队、app_console 取指令解析执行、
 *           app_tick 10ms 心跳驱动闪烁与呼吸、motor_ramp 20ms 推进电机。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_STATE_MENU = 0,
    APP_STATE_TASK1,
    APP_STATE_TASK2,
    APP_STATE_TASK3,
    APP_STATE_TASK4,
    APP_STATE_TASK5,
    APP_STATE_MAX,
} app_state_t;

typedef enum {
    APP_SRC_UART0 = 0,      /* USB 串口 */
    APP_SRC_BT,             /* HC-04D 蓝牙透传 */
} app_src_t;

void app_start(void);

/* 指令入队（在 UART / BLE 接收任务的回调里调用，只入队不阻塞） */
void app_post_line(const char *line, app_src_t src);

/* 真正的解析 + 执行（在 app_console 任务里跑） */
void app_handle_line(const char *line, app_src_t src);

app_state_t app_get_state(void);
const char      *app_state_name(void);

#ifdef __cplusplus
}
#endif

#endif
