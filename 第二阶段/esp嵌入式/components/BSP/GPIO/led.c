#include "led.h"

#include "driver/ledc.h"
#include "esp_log.h"

static bool     s_pwm = true;             /* 引脚当前是否由 LEDC 驱动 */
static uint32_t s_freq = LED_PWM_FREQ;
static uint8_t  s_res  = LED_PWM_RES;     /* 实际生效的分辨率，可能被下调 */

void led_gpio_init(void)
{
    gpio_config_t gpio_init_struct = {0};
    gpio_init_struct.intr_type    = GPIO_INTR_DISABLE;
    gpio_init_struct.mode         = GPIO_MODE_INPUT_OUTPUT;
    gpio_init_struct.pull_up_en   = GPIO_PULLUP_ENABLE;
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_init_struct.pin_bit_mask = 1ull << LED_GPIO_PIN;

    /* 先把 LEDC 从引脚上摘下来，gpio_config 才会把输出信号切回普通 GPIO */
    ledc_stop(LED_PWM_MODE, LED_PWM_CHANNEL, 0);
    gpio_config(&gpio_init_struct);

    s_pwm = false;
    LED(0);
}

void led_pwm_init(void)
{
    /* 2^res * freq 不能超过 LEDC 源时钟(APB 80MHz) */
    while (s_res > 1 && ((uint64_t)(1u << s_res) * s_freq) > 80000000ULL)
    {
        s_res--;
    }

    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LED_PWM_MODE,
        .duty_resolution = (ledc_timer_bit_t)s_res,
        .timer_num       = LED_PWM_TIMER,
        .freq_hz         = s_freq,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {
        .gpio_num   = LED_GPIO_PIN,
        .speed_mode = LED_PWM_MODE,
        .channel    = LED_PWM_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LED_PWM_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    ledc_set_duty(LED_PWM_MODE, LED_PWM_CHANNEL, 0);
    ledc_update_duty(LED_PWM_MODE, LED_PWM_CHANNEL);

    s_pwm = true;
}

void led_init(void)
{
    led_pwm_init();
}

void led_set_brightness(uint8_t brightness)
{
    uint32_t duty_max;
    uint32_t duty;

    if (brightness > LED_BRIGHTNESS_MAX) {
        brightness = LED_BRIGHTNESS_MAX;
    }

    if (!s_pwm) {
        LED(brightness > 0);      /* GPIO 模式没有亮度，退化成开 / 关 */
        return;
    }

    /* duty = brightness / 100 * (2^res - 1)，先乘后除避免算成 0 */
    duty_max = (1u << s_res) - 1u;
    duty     = (uint32_t)brightness * duty_max / LED_BRIGHTNESS_MAX;

    ledc_set_duty(LED_PWM_MODE, LED_PWM_CHANNEL, duty);
    ledc_update_duty(LED_PWM_MODE, LED_PWM_CHANNEL);
}

uint8_t led_get_brightness(void)
{
    uint32_t duty_max;
    uint32_t duty;

    if (!s_pwm) {
        return led_gpio_read() ? LED_BRIGHTNESS_MAX : 0;
    }

    duty_max = (1u << s_res) - 1u;
    duty     = ledc_get_duty(LED_PWM_MODE, LED_PWM_CHANNEL);

    if (duty > duty_max) {
        return LED_BRIGHTNESS_MAX;
    }
    return (uint8_t)(duty * LED_BRIGHTNESS_MAX / duty_max);
}

void led_set_on(bool on)
{
    if (on && led_get_brightness() == 0) {
        led_set_brightness(LED_BRIGHTNESS_MAX);
    } else if (!on) {
        led_set_brightness(0);
    }
}

bool led_is_on(void)
{
    return led_get_brightness() > 0;
}

void led_toggle(void)
{
    if (led_is_on()) {
        led_set_brightness(0);
    } else {
        led_set_brightness(LED_BRIGHTNESS_MAX);
    }
}

bool led_is_pwm(void)
{
    return s_pwm;
}

bool led_gpio_read(void)
{
    return gpio_get_level(LED_GPIO_PIN) != 0;
}

void led_pwm_set_freq(uint32_t freq_hz)
{
    uint8_t keep = led_get_brightness();

    if (freq_hz < 100)   freq_hz = 100;
    if (freq_hz > 20000) freq_hz = 20000;

    s_freq = freq_hz;
    s_res  = LED_PWM_RES;
    led_pwm_init();
    led_set_brightness(keep);     /* 调频率时不要把灯闪灭 */
}

uint32_t led_pwm_get_freq(void)
{
    return s_freq;
}

uint8_t led_pwm_get_resolution(void)
{
    return s_res;
}
