/* FinalWork —— FOCUS 2026 秋季招新控制组考核 · 阶段二
 * 嵌入式基础（方向二：ESP-IDF）整合版工程
 *
 * 上电后串口（UART0，115200 8N1）打印主菜单，输入 Task1 ~ Task5 切换到对应演示：
 *   Task1 GPIO 点灯 / Task2 LEDC 呼吸灯 / Task3 串口协议点灯
 *   Task4 Wi-Fi + 蓝牙点灯 / Task5 直流电机 + 编码器测速 + PID 闭环
 * 其它指令：HELP | STATUS | INFO | MENU
 *
 * 外设驱动都在 components/BSP/<外设>/ 里，入口只负责启动。
 */

#include "app.h"

void app_main(void)
{
    app_start();
}
