#ifndef __PID_H_
#define __PID_H_

/* TASK5 拓展：PID 速度闭环（位置式 PI(D)）
 *
 * 回路：目标转速 - 实际转速(PCNT 测得) -> PID -> 更新 PWM 占空比，控制周期 40ms
 *   P 比例：快速消除偏差
 *   I 积分：消除稳态误差（电机有死区，没有 I 永远差一截）
 *   D 微分：抑制超调，作用在测量值上并加一阶低通；速度环噪声大，默认 kd = 0
 *
 * 两个工程处理：条件积分抗饱和、微分作用在测量值上。
 * 本文件不依赖 ESP-IDF，可以在 PC 上离线编译测试。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PID_D_LPF_ALPHA     0.30f   /* 微分项一阶低通系数 */
#define PID_I_DEADBAND      1.0f    /* 偏差死区：进入后不再积分 */

typedef struct {
    float kp, ki, kd;

    float out_min, out_max;     /* 输出限幅（占空比 0~100） */

    float i_term;               /* 积分项累积值（已乘 ki） */
    float i_min, i_max;         /* 积分限幅（抗饱和） */

    float prev_meas;            /* 上一次测量值，用来算微分 */
    float d_filt;               /* 低通后的微分值 */
    uint8_t started;            /* 第一次调用时没有历史值，微分先不输出 */
} pid_t;

void  pid_init(pid_t *pid, float kp, float ki, float kd, float out_min, float out_max);
void  pid_set_gains(pid_t *pid, float kp, float ki, float kd);   /* 在线改参数，不清积分 */
void  pid_reset(pid_t *pid);                                     /* 停机/换向时清积分 */
float pid_update(pid_t *pid, float target, float measured, float dt_s);

#ifdef __cplusplus
}
#endif

#endif
