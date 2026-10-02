#ifndef __BREATH_H_
#define __BREATH_H_

/* TASK2 呼吸灯：非阻塞三角波渐变，由 APP 的 10ms 心跳调用 breath_tick() 推进 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BREATH_TICK_MS      20      /* 每 20ms 推进一档 */
#define BREATH_STEP_PCT     1       /* 每档 1%，0->100 用 2s */

void    breath_start(void);
void    breath_stop(void);
uint8_t breath_is_running(void);
void    breath_tick(uint32_t elapsed_ms);
uint8_t breath_get_duty(void);
void    breath_set_step(uint32_t tick_ms, uint8_t step_pct);

#ifdef __cplusplus
}
#endif

#endif
