#ifndef __ENCODER_H_
#define __ENCODER_H_

/* TASK5 拓展：编码器测速（ESP32-S3 PCNT 硬件脉冲计数）
 *
 *   A 相 -> GPIO7，B 相 -> GPIO8（两路都要接，只接一路只能 1 倍频且测不出方向）
 *   倍频方式：AB 正交 4 倍频（等效 STM32 定时器编码器模式）
 *
 * 换算：每转脉冲数 = 线数(PPR) × 减速比 × 倍频
 *       RPM = counts × 60000 / (COUNTS_PER_REV × dt_ms)
 *
 * 下面三个宏按手上电机改，可以用 ENCCAL 指令标定。
 * 默认 13 线 × 48 减速比 × 4 倍频 = 2496 计数/圈（最常见的黄色 TT 编码器电机）。
 */

#include <stdint.h>

#include "driver/gpio.h"

/* ------------------------------ 接线（只改这里） ------------------------------ */
#define ENCODER_A_GPIO          GPIO_NUM_7
#define ENCODER_B_GPIO          GPIO_NUM_8

/* ------------------------------ 机械参数 ------------------------------ */
#define ENCODER_PPR             13      /* 编码器线数（减速前电机轴每转 A 相脉冲数） */
#define ENCODER_GEAR_RATIO      48      /* 减速比 */
#define ENCODER_MULTIPLIER      4       /* AB 正交 4 倍频 */

#define ENCODER_COUNTS_PER_REV  (ENCODER_PPR * ENCODER_GEAR_RATIO * ENCODER_MULTIPLIER)

/* PCNT 计数上下限，兜底保护 */
#define ENCODER_COUNT_LIMIT     30000

/* 毛刺滤波：窄于 1us 的脉冲当噪声丢掉（1us @ 2496 计数/圈 ≈ 24000 RPM 才会被滤掉） */
#define ENCODER_GLITCH_NS       1000

typedef struct {
    int32_t  delta;      /* 本采样周期内的脉冲数（带符号） */
    int32_t  total;      /* 上电以来累计脉冲数 */
    uint32_t dt_ms;      /* 本次采样的实际间隔（esp_timer 量的） */
    float    rpm;        /* 转速大小（RPM，非负） */
    float    rpm_signed; /* 带符号转速，用来检查 A/B 是否接反 */
} encoder_sample_t;

float encoder_counts_to_rpm(int32_t counts, uint32_t dt_ms);

void encoder_init(void);
void encoder_sample(encoder_sample_t *out);
int32_t encoder_get_total(void);

/* 排障接口："pulses 一直是 0" 时用来区分"没信号"和"读计数失败" */
void     encoder_get_levels(int *a_level, int *b_level);
int      encoder_peek_raw(void);
uint32_t encoder_get_sample_count(void);
int      encoder_get_last_error(void);

#endif
