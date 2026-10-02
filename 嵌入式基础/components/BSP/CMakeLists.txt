# BSP —— 板级支持包：按外设 / 功能拆分，整体作为单个组件，由本文件统一注册所有子目录。
#
#   GPIO/    TASK1 GPIO 点灯（也是 LED 的引脚 / 亮度抽象层）
#   LEDC/    TASK2 LEDC PWM 封装 + 呼吸灯渐变
#   UART/    TASK3 串口驱动（中断接收 + 环形缓冲 + 按行回调）
#   CMD/     指令解析层（TASK5 电机指令表 + APP 复用的解析工具）
#   WIFI/    TASK4 Wi-Fi STA
#   HTTP/    TASK4 esp_http_server + 内嵌点灯网页
#   BLE/     TASK4 外接 HC-04D 蓝牙模块（UART1 透传 + AT 探测）
#   MOTOR/   TASK5 电机驱动（TB6612 / L9110S + LEDC 调速 + 方向）
#   ENCODER/ TASK5 拓展 PCNT 正交 4 倍频测速
#   PID/     TASK5 拓展 PID 速度闭环
#   APP/     状态机 + 主菜单 + Task1~Task5 分发
#
# TASK6（SPI / ST7789V_LCD / IIC / FT6336U_TOUCH / LVGL_UI）未完成，不建目录、不注册。

set(src_dirs
            GPIO
            LEDC
            UART
            CMD
            WIFI
            HTTP
            BLE
            MOTOR
            ENCODER
            PID
            APP)

set(include_dirs
            ${src_dirs})

set(requires
            driver
            esp_driver_gpio
            esp_driver_ledc
            esp_driver_uart
            esp_driver_pcnt
            esp_timer
            esp_wifi
            esp_http_server
            esp_netif
            esp_event
            nvs_flash)

# 把点灯网页编进固件，链接后可用 _binary_led_html_start / _end 访问
set(embed_files
            "${CMAKE_CURRENT_LIST_DIR}/HTTP/led.html")

idf_component_register(SRC_DIRS ${src_dirs}
                       INCLUDE_DIRS ${include_dirs}
                       REQUIRES ${requires}
                       EMBED_FILES ${embed_files})

# 注意：01_LED~08_wifista 里的 "-Wno-error=format=-Wno-format" 是少了个空格的笔误选项，
# 实际不生效。这里直接不写，格式串检查保持开启。
component_compile_options(-ffast-math -O3)
