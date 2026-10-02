#include "encoder.h"

#include "driver/pulse_cnt.h"
#include "esp_err.h"
#include "esp_timer.h"

/* TASK5 拓展：PCNT 编码器测速
 *
 * 用 PCNT 而不是 GPIO 外部中断：计数由硬件完成（CPU 占用为零）、自带毛刺滤波、
 * 两只通道互为正交解码天然给出方向。
 * 采样方式："读一次、清一次"的增量式测速，每 40ms 读计数并清零。
 */

static pcnt_unit_handle_t s_unit = NULL;
static int64_t  s_last_us = 0;
static int32_t  s_total = 0;
static uint32_t s_samples = 0;
static int      s_last_err = 0;

float encoder_counts_to_rpm(int32_t counts, uint32_t dt_ms)
{
    if (dt_ms == 0) {
        return 0.0f;
    }
    /* RPM = counts ÷ COUNTS_PER_REV ÷ (dt_ms/60000) */
    return ((float)counts * 60000.0f) / ((float)ENCODER_COUNTS_PER_REV * (float)dt_ms);
}

void encoder_init(void)
{
    pcnt_unit_config_t unit_config = {
        .low_limit = -ENCODER_COUNT_LIMIT,
        .high_limit = ENCODER_COUNT_LIMIT,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &s_unit));

    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = ENCODER_GLITCH_NS,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(s_unit, &filter_config));

    /* A 当边沿信号时 B 当电平信号，反之亦然 —— 这就是正交 4 倍频 */
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = ENCODER_A_GPIO,
        .level_gpio_num = ENCODER_B_GPIO,
    };
    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = ENCODER_B_GPIO,
        .level_gpio_num = ENCODER_A_GPIO,
    };
    pcnt_channel_handle_t chan_a = NULL;
    pcnt_channel_handle_t chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(s_unit, &chan_a_config, &chan_a));
    ESP_ERROR_CHECK(pcnt_new_channel(s_unit, &chan_b_config, &chan_b));

    /* 上升沿/下降沿一增一减，另一路电平决定增还是减，因此正转增、反转减 */
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                                PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                                 PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                                PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                                 PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_unit_enable(s_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(s_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(s_unit));

    s_last_us = esp_timer_get_time();
    s_total = 0;
}

void encoder_sample(encoder_sample_t *out)
{
    int count = 0;
    int64_t now = 0;
    int64_t dt_us = 0;

    if (out == NULL) {
        return;
    }

    /* 没初始化过就返回 0，而不是让 pcnt_unit_get_count(NULL) 触发断言重启 */
    if (s_unit == NULL) {
        out->delta = 0;
        out->total = 0;
        out->dt_ms = 0;
        out->rpm = 0.0f;
        out->rpm_signed = 0.0f;
        return;
    }

    /* 读计数失败不当成 0：错误码留给 ENCCAL / ENCDBG 显示，
     * 否则"PCNT 读不出来"和"编码器没信号"在串口上长得一模一样 */
    s_last_err = (int)pcnt_unit_get_count(s_unit, &count);
    if (s_last_err != ESP_OK) {
        count = 0;
    }
    (void)pcnt_unit_clear_count(s_unit);
    s_samples++;

    now = esp_timer_get_time();
    dt_us = now - s_last_us;
    s_last_us = now;
    if (dt_us <= 0) {
        dt_us = 1;
    }

    s_total += count;

    out->delta = count;
    out->total = s_total;
    out->dt_ms = (uint32_t)(dt_us / 1000);
    if (out->dt_ms == 0) {
        out->dt_ms = 1;
    }
    out->rpm = encoder_counts_to_rpm(count, out->dt_ms);
    if (out->rpm < 0.0f) {
        out->rpm = -out->rpm;           /* 控制回路只关心转速大小，方向由 DIR 决定 */
    }
    out->rpm_signed = encoder_counts_to_rpm(count, out->dt_ms);
}

int32_t encoder_get_total(void)
{
    return s_total;
}

void encoder_get_levels(int *a_level, int *b_level)
{
    /* PCNT 只是通过 GPIO 矩阵旁路取信号，引脚本身的输入寄存器照样能读 */
    if (a_level != NULL) {
        *a_level = gpio_get_level(ENCODER_A_GPIO);
    }
    if (b_level != NULL) {
        *b_level = gpio_get_level(ENCODER_B_GPIO);
    }
}

int encoder_peek_raw(void)
{
    int count = 0;

    if (s_unit == NULL) {
        return 0;
    }
    if (pcnt_unit_get_count(s_unit, &count) != ESP_OK) {
        return 0;
    }
    return count;
}

uint32_t encoder_get_sample_count(void)
{
    return s_samples;
}

int encoder_get_last_error(void)
{
    return s_last_err;
}
