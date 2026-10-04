#include "pid.h"

#include <stddef.h>

/* TASK5 拓展：位置式 PI(D)
 *
 *   out    = kp*e + i_term + kd*d(meas)/dt      (e = target - measured)
 *   i_term += ki * e * dt                       (条件积分抗饱和)
 * 微分取"测量值变化率"的负号，等价于 -kd*de/dt，但目标阶跃时不会打尖峰。
 */

static float clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

void pid_init(pid_t *pid, float kp, float ki, float kd, float out_min, float out_max)
{
    if (pid == NULL) {
        return;
    }

    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->out_min = out_min;
    pid->out_max = out_max;

    /* 积分项单位就是输出单位，直接拿输出范围限幅（最简单的抗饱和） */
    pid->i_min = out_min;
    pid->i_max = out_max;

    pid->i_term = 0.0f;
    pid->prev_meas = 0.0f;
    pid->d_filt = 0.0f;
    pid->started = 0;
}

void pid_set_gains(pid_t *pid, float kp, float ki, float kd)
{
    if (pid == NULL) {
        return;
    }
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
}

void pid_reset(pid_t *pid)
{
    if (pid == NULL) {
        return;
    }
    pid->i_term = 0.0f;
    pid->prev_meas = 0.0f;
    pid->d_filt = 0.0f;
    pid->started = 0;
}

float pid_update(pid_t *pid, float target, float measured, float dt_s)
{
    float err, p_term, d_raw, d_term, out;
    int sat_high, sat_low, push_high, push_low, in_deadband;

    if (pid == NULL) {
        return 0.0f;
    }
    if (dt_s <= 0.0f) {
        dt_s = 0.001f;      /* 防御：dt 不合法时按 1ms 算，绝不除零 */
    }

    err = target - measured;

    /* ---------- P ---------- */
    p_term = pid->kp * err;

    /* ---------- D：作用在测量值上，再一阶低通 ---------- */
    if (pid->started) {
        d_raw = -(measured - pid->prev_meas) / dt_s;    /* 负号：转速上升 -> 抑制输出 */
    } else {
        d_raw = 0.0f;
        pid->started = 1;
    }
    pid->prev_meas = measured;
    pid->d_filt += PID_D_LPF_ALPHA * (d_raw - pid->d_filt);
    d_term = pid->kd * pid->d_filt;

    /* ---------- I：条件积分（死区 + 抗饱和） ---------- */
    out = p_term + pid->i_term + d_term;

    sat_high    = (out >= pid->out_max);
    sat_low     = (out <= pid->out_min);
    push_high   = (err > PID_I_DEADBAND);
    push_low    = (err < -PID_I_DEADBAND);
    in_deadband = (!push_high && !push_low);

    if (!in_deadband && !((sat_high && push_high) || (sat_low && push_low))) {
        pid->i_term += pid->ki * err * dt_s;
        pid->i_term = clampf(pid->i_term, pid->i_min, pid->i_max);
    }

    /* ---------- 合成 + 输出限幅 ---------- */
    out = p_term + pid->i_term + d_term;
    out = clampf(out, pid->out_min, pid->out_max);

    return out;
}
