#ifndef __LED_H_
#define __LED_H_

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/ledc.h"

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LED_GPIO_PIN         GPIO_NUM_1
#define LED_BRIGHTNESS_MAX   100

#define LED_PWM_MODE         LEDC_LOW_SPEED_MODE
#define LED_PWM_TIMER        LEDC_TIMER_0
#define LED_PWM_CHANNEL      LEDC_CHANNEL_0

#define LED_PWM_FREQ         CONFIG_BSP_LED_PWM_FREQ_HZ
#define LED_PWM_RES          CONFIG_BSP_LED_PWM_RESOLUTION

/* ------------------------------- 旧接口兼容 ------------------------------ */
enum GPIO_OUTPUT_STATE
{
    PIN_RESET,
    PIN_SET
};

#define LED(X) do { X ? \
                        gpio_set_level(LED_GPIO_PIN, PIN_SET):\
                        gpio_set_level(LED_GPIO_PIN, PIN_RESET);\
                    }while(0)

#define LED_TOGGLE()    do{ gpio_set_level(LED_GPIO_PIN, !gpio_get_level(LED_GPIO_PIN));}while(0)

/* ------------------------------- 对外接口 ------------------------------- */
void    led_gpio_init(void);                         /* TASK1：引脚归普通 GPIO 驱动 */
void    led_pwm_init(void);                          /* TASK2~4：引脚归 LEDC 驱动 */
void    led_init(void);

void    led_set_brightness(uint8_t brightness);      /* 0~100，越界自动截断 */
uint8_t led_get_brightness(void);
void    led_set_on(bool on);
bool    led_is_on(void);
void    led_toggle(void);

bool    led_is_pwm(void);                            /* 当前引脚是否由 LEDC 驱动 */
bool    led_gpio_read(void);

/* 在线改 PWM 频率（TASK2 的 FREQ 指令），分辨率超时钟上限时自动下调 */
void     led_pwm_set_freq(uint32_t freq_hz);
uint32_t led_pwm_get_freq(void);
uint8_t  led_pwm_get_resolution(void);

#ifdef __cplusplus
}
#endif

#endif
