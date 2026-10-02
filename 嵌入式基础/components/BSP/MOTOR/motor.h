#ifndef __MOTOR_H_
#define __MOTOR_H_

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ledc.h"

#include "encoder.h"
#include "pid.h"

/* TASK5：直流电机控制（TB6612FNG / L9110S）
 *
 *   PWMA ---- LEDC PWM 调速，20kHz / 10bit
 *   AIN1 ---- 方向位 1
 *   AIN2 ---- 方向位 2
 *   STBY 直接接 3.3V（TB6612 内部无上拉，必须外部拉高）
 *
 * 接线：GPIO4->PWMA  GPIO5->AIN1  GPIO6->AIN2
 *       电机 5V 由独立电源供给，必须与开发板共地
 *
 * 四种模式（同一时刻只有一种生效）：
 *   OFF    停止（占空比 0 + 短刹车）
 *   OPEN   开环：直接给占空比（SPEED），走 2%/20ms 无级渐变
 *   AUTO   开环自动渐变：慢->中->快->中->慢，两端自动换向
 *   CLOSED PID 速度闭环（SET_RPM），占空比由 PID 每 40ms 算出
 */

#define MOTOR_PWM_GPIO          GPIO_NUM_4   /* PWMA */
#define MOTOR_DIR_GPIO_A        GPIO_NUM_5   /* AIN1 */
#define MOTOR_DIR_GPIO_B        GPIO_NUM_6   /* AIN2 */

/* 20kHz 超出人耳听觉范围，电机不会啸叫；10bit -> 占空比 0~1023 */
#define MOTOR_PWM_TIMER         LEDC_TIMER_1 /* 给 TASK2 呼吸灯让出 TIMER_0 */
#define MOTOR_PWM_CHANNEL       LEDC_CHANNEL_1
#define MOTOR_PWM_FREQ_HZ       20000
#define MOTOR_PWM_RES           LEDC_TIMER_10_BIT
#define MOTOR_PWM_CLK           LEDC_USE_APB_CLK

/* 每 20ms 改变 2% 占空比 -> 0~100% 约 1 秒，不阻塞 */
#define MOTOR_RAMP_PERIOD_MS    20
#define MOTOR_RAMP_STEP_PCT     2

/* ON/OFF/SPEED/DIR/AUTO 都算"显式指令"，执行时一律先退出 AUTO 与闭环，
 * 否则 AUTO 的渐变任务和 PID 会立刻把目标 / 占空比改写掉 */

/* ------------------------- 拓展：PID 速度闭环参数 ------------------------- */
#define MOTOR_PID_DIV           2       /* 2 拍(20ms) = 40ms 跑一次测速 + PID */
#define MOTOR_PID_TELEM_DIV     5       /* 每 5 次 PID（200ms）回一行实测转速 */
#define MOTOR_RPM_MAX           600     /* TT 马达 5V 空载约 250~300 RPM */
#define MOTOR_PID_KP            0.35f
#define MOTOR_PID_KI            0.60f
#define MOTOR_PID_KD            0.00f   /* 速度环噪声大，D 默认不用 */

/* PID 输出限速：每周期最多改变这么多占空比，保护齿轮箱与 5V 电源 */
#define MOTOR_PID_SLEW_PCT      15

/* 编码器排障观察（ENCDBG 指令） */
#define MOTOR_ENC_WATCH_MS      6000
#define MOTOR_ENC_WATCH_DIV     10      /* 10 拍(200ms) 打一行 */

typedef enum {
    MOTOR_DIR_FWD = 0,   /* 正转 */
    MOTOR_DIR_REV = 1,   /* 反转 */
} motor_dir_t;

typedef struct {
    uint8_t run;              /* 1 = 运行中 */
    motor_dir_t dir;          /* 当前方向 */
    uint8_t auto_ramp;        /* 1 = 自动渐变循环 */
    uint8_t target_pct;       /* 目标占空比 0~100（闭环时跟随 PID 输出） */
    uint8_t cur_pct;          /* 当前实际占空比 0~100 */
    uint8_t ramp_reversed;    /* 自动渐变走向：0 = 递增 */
    ledc_config_t ledc;

    /* ------------------------- 拓展：速度闭环 ------------------------- */
    uint8_t closed_loop;
    uint16_t target_rpm;
    float rpm;
    uint8_t cl_tick;
    uint8_t telem_div;
    encoder_sample_t enc;
    pid_t pid;
} motor_t;

void motor_init(void);
void motor_start(void);
void motor_stop(void);
void motor_set_dir(motor_dir_t dir);
void motor_set_speed(uint8_t pct);
void motor_ramp_enable(uint8_t enable);
void motor_ramp_task(void *arg);

/* ------------------------- 拓展：PID 速度闭环接口 ------------------------- */
void     motor_closed_loop_enable(uint8_t enable);
void     motor_set_target_rpm(uint16_t rpm);
void     motor_pid_set_gains(float kp, float ki, float kd);
void     motor_enc_watch_start(uint32_t duration_ms);
uint8_t  motor_enc_watch_active(void);
uint16_t motor_get_target_rpm(void);
float    motor_get_rpm(void);
uint8_t  motor_is_closed_loop(void);

uint8_t     motor_get_speed(void);
uint8_t     motor_get_target(void);
motor_dir_t motor_get_dir(void);
uint8_t     motor_is_running(void);
uint8_t     motor_is_auto(void);

#endif
