#include "ledc.h"
#include "esp_log.h"

/* 满量程是 2^n - 1，原式 (2^n * duty)/100 在 duty=100 时会得到 2^n，
 * 硬件截断后 "最亮" 反而变成 0 */
uint32_t ledc_duty_pow(uint32_t duty, uint8_t m, uint8_t n)
{
    uint32_t result = 1;
    uint32_t full = (1u << n) - 1u;

    while(n--)
    {
        result *= m;
    }

    if (duty > 100)
    {
        duty = 100;
    }

    result = (uint32_t)(((uint64_t)result * duty) / 100u);

    return (result > full) ? full : result;
}

void ledc_init(ledc_config_t *ledc_config)
{
    /* 2^n * freq 不能超过 LEDC 源时钟(APB 80MHz)，超了 ledc_timer_config 报错且没有波形 */
    while (ledc_config->duty_resolution > LEDC_TIMER_1_BIT &&
           ((uint64_t)(1u << ledc_config->duty_resolution) * ledc_config->freq_hz) > 80000000ULL)
    {
        ESP_LOGW("LEDC", "PWM 超时钟上限，分辨率降 1 位");
        ledc_config->duty_resolution--;
    }

    ledc_config->duty = ledc_duty_pow(ledc_config->duty, 2, ledc_config->duty_resolution);

    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = ledc_config->duty_resolution,
        .clk_cfg = ledc_config->clk_cfg,
        .freq_hz = ledc_config->freq_hz,
        .timer_num = ledc_config->timer_num
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .intr_type = LEDC_INTR_DISABLE,
        .channel = ledc_config->channel,
        .duty = ledc_config->duty,
        .gpio_num = ledc_config->gpio_num,
        .hpoint = 0,
        .timer_sel = ledc_config->timer_num,
    };
    ledc_channel_config(&ledc_channel);
}

void ledc_pwm_set_duty(ledc_config_t *ledc_config, uint16_t duty)
{
    ledc_config->duty = ledc_duty_pow(duty, 2, ledc_config->duty_resolution);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, ledc_config->channel, ledc_config->duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, ledc_config->channel);
}
