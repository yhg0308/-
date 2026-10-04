# FinalWork —— FOCUS 2026 秋季招新控制组考核 · 阶段二

> **嵌入式基础（方向二：ESP-IDF）整合版工程**
> 一句话：一块 ESP32-S3 上把「GPIO 点灯 → LEDC 呼吸灯 → 串口协议点灯 → 无线点灯 → 直流电机 + 编码器 + PID 闭环」
> 全部收进同一个工程，**在串口里输入 `Task1` ~ `Task5` 就能切换对应的演示功能**。

技术栈：`ESP32-S3-N16R8` + `ESP-IDF v5.5.5` + `FreeRTOS` + `esp_wifi / esp_http_server` +
`LEDC(PWM)` + `PCNT`（编码器）+ `esp_timer`



## 1. 开发环境与依赖

| 项目 | 版本 / 说明 |
|---|---|
| 芯片目标 | **ESP32-S3-N16R8**（16MB Flash / 8MB Octal PSRAM，带排针焊接版） |
| 开发框架 | **ESP-IDF v5.5.5**|
| 开发方式 | Windows + ESP-IDF 官方安装器 + vscode|
| 串口 | UART0，**115200 8N1**，|
| 组件依赖 |`driver`、`esp_driver_gpio`、`esp_driver_ledc`、`esp_driver_uart`、`esp_driver_pcnt`、`esp_timer`、`esp_wifi`、`esp_http_server`、`esp_netif`、`esp_event`、`nvs_flash` |

---

## 2. 硬件引脚

> 引脚全部集中在各模块头文件顶部的宏里（`led.h` / `motor.h` / `encoder.h` / `main/Kconfig.projbuild`），
> 换板子只改这些宏.

### 2.1 LED（TASK1 / TASK2 / TASK3 / TASK4）

| 信号 | GPIO | 方向 | 说明 |
|---|---|---|---|
| LED | GPIO1 | 输出 | TASK1 用普通 GPIO 推挽输出；TASK2~4 由 `LEDC_CHANNEL_0 / TIMER_0` 接管做 PWM 调光 |

### 2.2 控制台串口（TASK3 / 所有 Task 的入口）

| 信号 | GPIO | 方向 | 说明 |
|---|---|---|---|
| UART0 TXD | GPIO43 | 输出 | 日志 + 指令回复（`ESP_LOG` 与 `printf` 都走这里） |
| UART0 RXD | GPIO44 | 输入 | 指令输入，`Task1` ~ `Task5` 从这里进 |

### 2.3 直流电机 + 驱动（TASK5）

| 信号 | GPIO | 方向 | 说明 |
|---|---|---|---|
| PWMA | GPIO4 | 输出 | `LEDC_CHANNEL_1 / TIMER_1`，20kHz / 10bit 调速 |
| AIN1 | GPIO5 | 输出 | 方向位 1（推挽、无上下拉） |
| AIN2 | GPIO6 | 输出 | 方向位 2 |
| TB6612 VCC | — | 电源 | 接开发板 3.3V |
| TB6612 VM | — | 电源 | 接 **独立 5V 电源**（严禁用开发板 3.3V 直驱电机） |
| TB6612 GND | — | 电源 | **必须与开发板 GND 共地** |
| TB6612 STBY | — | 电源 | 直接接 3.3V（TB6612 内部无上拉，必须外部拉高） |
| TB6612 AO1/AO2 | — | 输出 | 接 TT 马达两根线 |

### 2.4 编码器（TASK5 拓展）

| 信号 | GPIO | 方向 | 说明 |
|---|---|---|---|
| 编码器 A 相 | GPIO7 | 输入 | PCNT 边沿信号 |
| 编码器 B 相 | GPIO8 | 输入 | PCNT 电平信号 |
| 编码器 VCC / GND | — | 电源 | 由开发板 3.3V 供电并与开发板共地 |

### 2.5 引脚冲突自查表

| 被占用 | 用途 |
|---|---|
| GPIO1 | LED |
| GPIO4 / 5 / 6 | 电机 PWM + 方向 |
| GPIO7 / 8 | 编码器 A / B |
| GPIO17 / 18 | 蓝牙 UART1 |
| GPIO19 / 20 | USB-Serial-JTAG（不要占用） |
| GPIO26 ~ 37 | 内部 Flash + Octal PSRAM（**绝对不要占用**） |
| GPIO43 / 44 | 控制台 UART0 |

---

## 3. 关键参数

### 3.1 PWM（LEDC）

| 用途 | 定时器 / 通道 | 频率 | 分辨率 | 占空比档数 | 选这个值的理由 |
|---|---|---|---|---|---|
| LED 调光 / 呼吸灯（TASK2/3/4） | `TIMER_0` / `CHANNEL_0` | **5000 Hz** | **13 bit** | 8192 | 1~5kHz 人眼看不到闪烁；13bit 让渐变足够平滑 |
| 电机调速（TASK5） | `TIMER_1` / `CHANNEL_1` | **20000 Hz** | **10 bit** | 1024 | 20kHz 超出人耳听觉，电机不会啸叫 |

### 3.2 编码器（TASK5 拓展）

| 参数 | 值 | 说明 |
|---|---|---|
| 编码器线数 PPR | **13** 线/圈 | 由淘宝卖家说明得出 |
| 减速比 | **48** | 由淘宝卖家说明得出 |
| 倍频方式 | **AB 正交 4 倍频** |无|
| 每转脉冲数 | 13 × 48 × 4 = **2496 计数/圈** |无|
| 测速周期 | **40 ms** | 与 PID 控制周期一致（PDF 建议 10~50ms） |

**实测转速换算公式**

```
RPM = 采样周期内的脉冲数 ÷ 每转脉冲数 ÷ 采样时间(分钟)
    = counts × 60000 / (COUNTS_PER_REV × dt_ms)
    = counts × 60000 / (2496 × dt_ms)
```

### 3.3 PID 速度闭环（TASK5 拓展）

| 参数 | 值 | 说明 |
|---|---|---|
| `kp` | **0.35** | 比例：按偏差成比例输出，快速消除偏差 |
| `ki` | **0.60** | 积分：累积历史偏差，消除稳态误差（电机有死区，没有 I 永远差一截） |
| `kd` | **0.00** | 微分：速度环噪声大，按 PDF 建议「只用 PI」 |
| 控制周期 | **40 ms** | `MOTOR_RAMP_PERIOD_MS(20ms) × MOTOR_PID_DIV(2)` |
| 输出限幅 | 0 ~ 100 % | 同时也是积分限幅（抗积分饱和） |
| 输出限速 | **15 %/周期** | 保护齿轮箱与 5V 电源，避免阶跃瞬间大电流冲击（0→100% 约 270ms） |
| 微分低通 | α = 0.30 | 微分作用在**测量值**上并一阶低通，目标阶跃时不会打尖峰 |
| 积分死区 | ±1.0 RPM | 偏差进入死区就不再积分，避免在目标附近来回蹭 |
| 转速上限 | 600 RPM | `MOTOR_RPM_MAX`，TT 马达 5V 空载约 250~300 RPM |
| 在线整定 | `PID <kp> <ki> <kd>` | 立即生效且不清积分，可以边跑边看阶跃响应 |

**在线整定方法（先 P 后 I）**

1. `PID 0.35 0 0` → `SET_RPM 250`，从小往大加 `kp`，取「开始明显震荡前的 50%~60%」；
2. 逐步加 `ki` 消除稳态误差（出现低频振荡就减小 `ki`）；
3. 每改一组参数，观察串口每 200ms 回的 `[RPM] target=… actual=… duty=…` 曲线（阶跃响应快慢 / 超调 / 稳态误差）。

### 3.4 串口

| 参数 | 值 |
|---|---|
| 波特率 | **115200** |
| 数据位 / 停止位 / 校验位 | **8 / 1 / None（8N1）** |
| 流控 | 无 |
| 接收方式 | **中断接收不用轮询**（不占 CPU） |

---

## 4. 控制逻辑（指令格式）

### 4.1 全局指令（任何状态都可用）

| 指令 | 说明 |
|---|---|
| `Task1` ~ `Task5` | 进入对应演示功能（等价 `T1`~`T5`） |
| `HELP` / `?` | 打印**当前 Task** 的指令表 |
| `STATUS` | 状态总览（LED / 呼吸 / 闪烁 / 电机 / Wi-Fi / HTTP） |
| `INFO` | 引脚 + 关键参数 + IDF 版本 + 空闲堆 |
| `MENU` / `BACK` / `EXIT` | 回主菜单 |

### 4.2 Task1 —— GPIO 点灯

| 指令 | 说明 |
|---|---|
| `ON` / `OFF` | GPIO1 输出高 / 低电平 |
| `TOGGLE` | 翻转电平 |
| `BLINK [ms]` | 自动闪烁，周期默认 500ms（允许 50~10000） |
| `READ` | 回读引脚真实电平（验证输出确实变了） |
| `STOP` | 停止闪烁并熄灭 |

进入 Task1 会自动点亮并开始 500ms 闪烁，串口同时打印实测电平。

### 4.3 Task2 —— LEDC 呼吸灯

| 指令 | 说明 |
|---|---|
| `BREATH` | 开始呼吸（0 → 100 → 0 平滑循环，默认 20ms 变化 1%，一个完整周期 4s） |
| `ON` / `OFF` | 常亮 100% / 熄灭 |
| `DUTY <0-100>` | 直接设定亮度（会结束呼吸） |
| `STEP <ms> [pct]` | 调整呼吸节奏，例如 `STEP 10 2` |
| `FREQ <hz>` | 重设 PWM 频率（100~20000），可现场观察分辨率被自动下调 |
| `STOP` | 停止呼吸并熄灭 |

### 4.4 Task3 —— UART 串口协议点灯

| 指令 | 说明 | 回复 |
|---|---|---|
| `ON` | 点亮 100% | `OK: LED ON (duty 100%)` |
| `OFF` | 熄灭 | `OK: LED OFF (duty 0%)` |
| `SPEED <0~100>` | 亮度百分比 | `OK: brightness=50%` |
| `BRIGHT <0~100>` | `SPEED` 的别名 | 同上 |
| `TOGGLE` | 翻转 | `OK: LED ON/OFF` |
| `BLINK <ms>` | 以 ms 为周期闪烁（50~10000） | `OK: blink 200 ms` |
| `BREATH` | 进入呼吸模式 | `OK: breath ON` |
| `FREQ <hz>` | 重设 PWM 频率 | `OK: PWM 2000 Hz / 13 bit` |
| `STOP` | 停止所有效果并熄灭 | `OK: all LED effects stopped` |
| 其它（未知指令 / 参数越界 / 格式错误 / 多余参数） | —— | **`ERROR`** |

### 4.5 Task4 —— 无线点灯

进入 Task4 时自动完成：连接 Wi-Fi（STA）→ 启动 HTTP 服务器
Wi-Fi 通过 DHCP 拿到 IP 后**自动在串口打印** `http://<IP>/`，也可以用 `IP` 指令随时查询。

| 指令 | 说明 |
|---|---|
| `WIFI` | 启动 / 重连 Wi-Fi STA |
| `IP` | 打印 IP、RSSI、连接状态 |
| `HTTP` / `HTTP OFF` | 启动 / 停止 HTTP 服务器 |
| `BLE` / `BLE OFF` | 初始化 HC-04D 并开启透传 / 关闭蓝牙串口 |
| `ON` / `OFF` / `TOGGLE` / `SPEED <0-100>` / `BREATH` / `BLINK <ms>` / `STOP` | |


### 4.6 Task5 —— 直流电机控制

| 指令 | 说明 |
|---|---|
| `ON` | 启动电机（目标为 0 时默认给 50%），退出 AUTO / 闭环 |
| `OFF` | 停止（目标置 0，平滑降到 0 后短刹车），退出 AUTO / 闭环 |
| `SPEED <0-100>` | 设置转速（占空比百分比），走 2%/20ms 的无级渐变 |
| `DIR F` / `DIR R` | 正转 / 反转（也支持 `FWD` / `REV`） |
| `AUTO` | 自动渐变循环：慢 → 中 → 快 → 中 → 慢，两端自动换向 |
| `MANUAL` | 退出自动渐变与闭环，保持当前占空比 |
| `STATUS` | 打印电机状态 |
| `SET_RPM <0-600>` | 设定目标转速并进入 **PID 速度闭环**；期间每 200ms 回一行 `[RPM] target=.. actual=.. duty=..` |
| `RPM` | 打印一次目标 / 实测转速 |
| `PID <kp> <ki> <kd>` | 在线整定 PID，立即生效且不清积分 |
| `OPEN` | 退出闭环，保持当前占空比 |
| `ENCCAL` | 打印编码器累计脉冲 / A,B 电平 / 采样次数 / PCNT 错误码 |
| `ENCDBG` | 连续 6 秒、每 200ms 打印一行编码器数据（排障用） |
| 未知 / 越界 / 缺参数 | **`ERROR`** |

**四种运行模式（同一时刻只有一种生效）**

| 模式 | 进入方式 | 占空比由谁决定 |
|---|---|---|
| `OFF` | `OFF` | 0 + 短刹车 |
| `OPEN` | `SPEED` / `ON` | 指令目标值，走 2%/20ms 渐变 |
| `AUTO` | `AUTO` | 渐变任务在 0~100 之间折返 |
| `CLOSED` | `SET_RPM` | PID 每 40ms 算一次 |

> 设计要点：`ON` / `OFF` / `SPEED` / `DIR` 都算「显式指令」，执行时**一律先退出 AUTO 与闭环**。
> 否则 AUTO 每 20ms 就改写目标、闭环的 PID 又会改写占空比，会出现
> 「OFF 停不下来、SPEED 设了没用、DIR 过一会儿自己转回去」。
>
> **验证 PID 的标准流程**：`SET_RPM 250` → 串口持续输出
> `[RPM] target=250 actual=xxx duty=yy%` → 观察转速从变化到稳定的快慢（阶跃响应）、
> 稳态误差与超调情况。

### 4.7 电机 / LED 状态真值表（TB6612）

| AIN1 | AIN2 | PWM | 电机状态 |
|---|---|---|---|
| 1 | 0 | duty | 正转（速度 = duty） |
| 0 | 1 | duty | 反转（速度 = duty） |
| 0 | 0 | 0 | 滑行停止（coast） |
| 1 | 1 | 0 | 短刹车（brake，停机时用，防止惯性拖着转） |

---

## 5. 项目结构

```
FinalWork/
├── CMakeLists.txt                  # 工程入口，注册 components/BSP
├── partitions-16Mib.csv            # 按照文中推荐的教程推荐的分区方式16MB Flash 分区表（nvs / phy / factory / vfs / storage）
├── sdkconfig.defaults              # ESP32-S3 + 16MB Flash + Octal PSRAM + UART0 115200 + FreeRTOS 1kHz
├── flash-app.ps1                   # 一键烧录(因本机时不时uart烧录失败，deepseek老师给出的解决方案)
├── README.md                       # 本文件
├── docs/
│   ├── images/                     # 运行效果截图
│   └── videos/                     # 运行效果视频          
├── main/
│   ├── main.c                      # 应用入口：只调用 app_start()
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild           # Wi-Fi SSID/密码、HTTP 端口、蓝牙引脚与波特率、LED PWM 参数
│   └── idf_component.yml           # 组件依赖（TASK6 的 lvgl 等已注释）
└── components/
    └── BSP/                        # BSP 整体是单个组件，由一个 CMakeLists.txt 统一注册所有子目录
        ├── CMakeLists.txt          # idf_component_register(SRC_DIRS ...) + EMBED_FILES led.html
        ├── GPIO/                   # TASK1  GPIO 点灯（led.c/led.h：引脚与亮度抽象层）
        ├── LEDC/                   # TASK2  LEDC PWM 通用封装（ledc.c）+ 呼吸灯渐变（breath.c）
        ├── UART/                   # TASK3  串口驱动：中断接收 + 环形缓冲 + 按行回调
        ├── CMD/                    # 指令解析层：通用解析工具 + TASK5 电机指令表
        ├── WIFI/                   # TASK4  Wi-Fi STA 连接（事件驱动 + 自动重连）
        ├── HTTP/                   # TASK4  esp_http_server + 内嵌网页 led.html
        ├── BLE/                    # TASK4  外接 HC-04D 双模蓝牙（UART1 透传 + AT 探测）
        ├── MOTOR/                  # TASK5  电机驱动（TB6612/L9110S + LEDC 无级调速 + 方向）
        ├── ENCODER/                # TASK5 拓展  PCNT 正交 4 倍频测速
        ├── PID/                    # TASK5 拓展  PID 速度闭环（位置式 PI(D) + 抗饱和）
        └── APP/                    # 应用层：状态机 + 主菜单 + Task1~Task5 分发与效果调度
```




## 6. 构建与烧录

构建使用用vscode中的扩展。

本机因不明原因使用VScode中的uart烧录时不时会莫名失败，经由deepseek老师疯狂烧token后，决定写了个脚本来烧录。

---

## 7. 版本号

| 组件 | 本工程实测 |
|---|---|
| ESP-IDF | **v5.5.5** |
| 芯片目标 | esp32s3 |
| 其余组件 | 均为 IDF v5.5.5 自带 |

## 8. 任务完成情况

| TASK | 内容 | 完成度 | 运行效果 |
|---|---|---|---|
| **TASK1** | GPIO 点灯 | ✅ 完成 | https://github.com/user-attachments/assets/3c0a1cde-3c77-4e96-9873-b40bfe2096fc|
| **TASK2** | LEDC PWM 呼吸灯 | ✅ 完成 | https://github.com/user-attachments/assets/ab713bd5-802d-411f-b401-39c49ed3aada|
| **TASK3** | UART 串口协议点灯 | ✅ 完成 |https://github.com/user-attachments/assets/6cd3defe-fb06-415a-adfc-4a733ba1f272|
| **TASK4** | 无线点灯：Wi-Fi STA + HTTP 网页 | ✅ 完成Wi-Fi / HTTP |https://github.com/user-attachments/assets/eed41500-3118-440b-80cc-ff4095556093|
| **TASK5 ** | 直流电机：启停 / 无级调速 / 方向 / 串口控制 / 自动渐变 | ✅ 完成 | https://github.com/user-attachments/assets/75a04eee-af2a-4231-935a-5b35bad761c6|
| **TASK6** | EEZ Studio + LVGL 触摸屏 | ❌ **未完成** | 无|

> 实际运行效果视频请见 [`docs/videos/`](docs/videos/)。

### 9.1 主要设计思路

1. 用**uart串口**来管理5个任务使其可以在同一个工程中运行
2. **所有周期性行为都是独立任务 + 毫秒时间基准**，没有一处阻塞延时。
3. 按照任务书里的建议，task5的电机操作挪用了呼吸灯和uart串口电灯的外设组件。

### 9.2 尚未解决的问题 / 下一步

* **TASK6 未完成**：没有做 EEZ Studio + LVGL v9 触摸屏界面。
* **TASK4 的蓝牙部分未完成** 
* **TASK5 的拓展PID部分未完善** 目前只有在ai帮助下完成的代码部分，实测部分不够完善。

---

## 10. 常见问题

| 现象 | 原因 | 解决 |
|---|---|---|
|呼吸灯会闪烁|计算占空比的函数溢出|在教学视频弹幕的帮助下，将占空比最大值设置为99后正常|
|urat烧录总是显示"MD5 of file does not match data in flash"|在deepseek的帮助下仍未找到原因|让deepseek老师写了个脚本帮我跑烧录|
||||
---
