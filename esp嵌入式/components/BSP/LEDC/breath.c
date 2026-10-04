#include "breath.h"

#include "led.h"

static uint8_t  s_running = 0;
static uint8_t  s_duty    = 0;
static uint8_t  s_rising  = 1;
static uint32_t s_acc_ms  = 0;

static uint32_t s_tick_ms = BREATH_TICK_MS;
static uint8_t  s_step    = BREATH_STEP_PCT;

void breath_start(void)
{
    s_running = 1;
    s_duty    = 0;
    s_rising  = 1;
    s_acc_ms  = 0;

    led_set_brightness(0);
}

void breath_stop(void)
{
    s_running = 0;
    s_acc_ms  = 0;
}

uint8_t breath_is_running(void)
{
    return s_running;
}

uint8_t breath_get_duty(void)
{
    return s_duty;
}

void breath_set_step(uint32_t tick_ms, uint8_t step_pct)
{
    if (tick_ms < 5)     tick_ms = 5;
    if (tick_ms > 500)   tick_ms = 500;
    if (step_pct < 1)    step_pct = 1;
    if (step_pct > 20)   step_pct = 20;

    s_tick_ms = tick_ms;
    s_step    = step_pct;
}

void breath_tick(uint32_t elapsed_ms)
{
    if (!s_running) {
        return;
    }

    s_acc_ms += elapsed_ms;

    /* while 而不是 if：心跳被抢占后补拍时亮度曲线不会被拉长 */
    while (s_acc_ms >= s_tick_ms)
    {
        s_acc_ms -= s_tick_ms;

        if (s_rising) {
            if (s_duty + s_step >= LED_BRIGHTNESS_MAX) {
                s_duty   = LED_BRIGHTNESS_MAX;
                s_rising = 0;
            } else {
                s_duty = (uint8_t)(s_duty + s_step);
            }
        } else {
            if (s_duty <= s_step) {
                s_duty   = 0;
                s_rising = 1;
            } else {
                s_duty = (uint8_t)(s_duty - s_step);
            }
        }
    }

    led_set_brightness(s_duty);
}
