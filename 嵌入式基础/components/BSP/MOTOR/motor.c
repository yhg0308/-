#include "motor.h"

#include "uart.h"

/* TASK5 直流电机控制（TB6612FNG + LEDC PWM）：基础 + 编码器测速 + PID 闭环
 *
 * 基础：GPIO 启停 / 正反转、LEDC PWM 无级调速、motor_ramp_task() 周期推进渐变
 * 拓展：每 40ms 用 encoder_sample() 测速换算 RPM，闭环时跑一次 PID 更新占空比，
 *       并每 200ms 从串口回一行 [RPM] target=.. actual=.. duty=..
 */

static motor_t s_motor = {
    .run = 0,
    .dir = MOTOR_DIR_FWD,
    .auto_ramp = 0,          /* 整合版上电不自动转，要演示就发 AUTO */
    .target_pct = 0,
    .cur_pct = 0,
    .ramp_reversed = 0,
    .ledc = {
        .clk_cfg = MOTOR_PWM_CLK,
        .timer_num = MOTOR_PWM_TIMER,
        .freq_hz = MOTOR_PWM_FREQ_HZ,
        .duty_resolution = MOTOR_PWM_RES,
        .channel = MOTOR_PWM_CHANNEL,
        .duty = 0,
        .gpio_num = MOTOR_PWM_GPIO,
    },
    .closed_loop = 0,
    .target_rpm = 0,
    .rpm = 0.0f,
    .cl_tick = 0,
    .telem_div = 0,
};

/* 方向引脚：推挽输出、无上下拉 */
static void tb6612_dir_gpio_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << MOTOR_DIR_GPIO_A) | (1ULL << MOTOR_DIR_GPIO_B),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

/* AIN1=1 AIN2=0 正转；AIN1=0 AIN2=1 反转；同高电平是短刹车 */
static void tb6612_set_dir(motor_dir_t dir)
{
    if (dir == MOTOR_DIR_FWD) {
        gpio_set_level(MOTOR_DIR_GPIO_A, 1);
        gpio_set_level(MOTOR_DIR_GPIO_B, 0);
    } else {
        gpio_set_level(MOTOR_DIR_GPIO_A, 0);
        gpio_set_level(MOTOR_DIR_GPIO_B, 1);
    }
}

static void tb6612_coast(void)
{
    gpio_set_level(MOTOR_DIR_GPIO_A, 0);
    gpio_set_level(MOTOR_DIR_GPIO_B, 0);
}

static void tb6612_brake(void)
{
    gpio_set_level(MOTOR_DIR_GPIO_A, 1);
    gpio_set_level(MOTOR_DIR_GPIO_B, 1);
}

/* 只改 PWM，不碰方向引脚 */
static void motor_apply_duty(uint8_t pct)
{
    if (pct > 100) {
        pct = 100;
    }
    s_motor.cur_pct = pct;
    ledc_pwm_set_duty(&s_motor.ledc, pct);
}

/* 拓展：PID 速度闭环的一个控制周期 */
static void motor_closed_loop_step(void)
{
    float dt_s = (float)s_motor.enc.dt_ms / 1000.0f;
    float out;

    if (s_motor.target_rpm == 0) {
        /* 目标 0：直接停 + 清积分，避免积分把电机顶起来 */
        motor_apply_duty(0);
        pid_reset(&s_motor.pid);
        s_motor.target_pct = 0;
        s_motor.run = 0;
        tb6612_brake();
    } else {
        out = pid_update(&s_motor.pid, (float)s_motor.target_rpm, s_motor.rpm, dt_s);

        /* 输出限速：保护齿轮箱与 5V 电源，也让阶跃响应不至于一上来就满占空比 */
        if (MOTOR_PID_SLEW_PCT > 0) {
            float delta = out - (float)s_motor.cur_pct;
            if (delta > (float)MOTOR_PID_SLEW_PCT) {
                out = (float)s_motor.cur_pct + (float)MOTOR_PID_SLEW_PCT;
            } else if (delta < -(float)MOTOR_PID_SLEW_PCT) {
                out = (float)s_motor.cur_pct - (float)MOTOR_PID_SLEW_PCT;
            }
        }
        if (out < 0.0f) {
            out = 0.0f;
        }
        if (out > 100.0f) {
            out = 100.0f;
        }

        motor_apply_duty((uint8_t)(out + 0.5f));
        s_motor.target_pct = s_motor.cur_pct;   /* 退出闭环时保持当前转速，不跳变 */
        s_motor.run = 1;
    }

    if (++s_motor.telem_div >= MOTOR_PID_TELEM_DIV) {
        s_motor.telem_div = 0;
        uart_printf("[RPM] target=%u actual=%.0f duty=%u%%\r\n",
                    (unsigned)s_motor.target_rpm, (double)s_motor.rpm,
                    (unsigned)s_motor.cur_pct);
    }
}

/* 拓展：编码器排障观察（A/B 不翻转 = 接线/供电问题；翻转但计数不涨 = PCNT 问题） */
static uint16_t s_enc_watch_ms = 0;
static uint8_t s_enc_watch_tick = 0;

static void motor_enc_watch_print(void)
{
    int a = 0;
    int b = 0;

    encoder_get_levels(&a, &b);
    uart_printf("[ENCDBG] A=%d B=%d delta=%ld total=%ld samples=%u err=%d\r\n",
                a, b,
                (long)s_motor.enc.delta,
                (long)encoder_get_total(),
                (unsigned)encoder_get_sample_count(),
                encoder_get_last_error());
}

void motor_enc_watch_start(uint32_t duration_ms)
{
    if (duration_ms > 60000) {
        duration_ms = 60000;
    }
    s_enc_watch_ms = (uint16_t)duration_ms;
    s_enc_watch_tick = 0;
}

uint8_t motor_enc_watch_active(void)
{
    return (s_enc_watch_ms > 0) ? 1 : 0;
}

void motor_init(void)
{
    tb6612_dir_gpio_init();
    tb6612_coast();                 /* 上电先滑行停止，避免上电瞬间乱转 */
    ledc_init(&s_motor.ledc);
    motor_apply_duty(0);

    s_motor.run = 0;
    s_motor.cur_pct = 0;
    s_motor.ramp_reversed = 0;
    tb6612_set_dir(s_motor.dir);

    s_motor.closed_loop = 0;
    s_motor.target_rpm = 0;
    s_motor.rpm = 0.0f;
    s_motor.cl_tick = 0;
    s_motor.telem_div = 0;
    pid_init(&s_motor.pid, MOTOR_PID_KP, MOTOR_PID_KI, MOTOR_PID_KD, 0.0f, 100.0f);
}

void motor_start(void)
{
    s_motor.auto_ramp = 0;
    s_motor.closed_loop = 0;
    pid_reset(&s_motor.pid);

    if (s_motor.target_pct == 0) {
        s_motor.target_pct = 50;    /* 只发 ON 时给个温和的默认转速 */
    }
    tb6612_set_dir(s_motor.dir);
    s_motor.run = 1;
}

void motor_stop(void)
{
    s_motor.auto_ramp = 0;
    s_motor.closed_loop = 0;
    s_motor.target_rpm = 0;
    pid_reset(&s_motor.pid);

    s_motor.run = 0;
    s_motor.target_pct = 0;   /* 目标置 0，渐变任务会平滑降到 0 再刹车 */
}

void motor_set_dir(motor_dir_t dir)
{
    s_motor.auto_ramp = 0;    /* AUTO 每次折返都会自动翻转方向 */
    s_motor.dir = dir;

    if (s_motor.closed_loop) {
        /* 闭环换向：先降到 0 再翻方向，并清积分重新加速 */
        motor_apply_duty(0);
        s_motor.target_pct = 0;
        pid_reset(&s_motor.pid);
    }
    if (s_motor.run) {
        tb6612_set_dir(dir);
    }
}

void motor_set_speed(uint8_t pct)
{
    if (pct > 100) {
        pct = 100;
    }
    s_motor.auto_ramp = 0;
    s_motor.closed_loop = 0;
    s_motor.target_rpm = 0;
    pid_reset(&s_motor.pid);

    s_motor.target_pct = pct;
    s_motor.run = (pct > 0) ? 1 : 0;
    if (s_motor.run) {
        tb6612_set_dir(s_motor.dir);
    }
}

void motor_ramp_enable(uint8_t enable)
{
    s_motor.auto_ramp = enable ? 1 : 0;

    if (s_motor.auto_ramp) {
        s_motor.closed_loop = 0;     /* AUTO 是开环演示，和闭环互斥 */
        s_motor.target_rpm = 0;
        pid_reset(&s_motor.pid);
        s_motor.run = 1;
        s_motor.ramp_reversed = 0;   /* 一律从递增开始 */
        /* 不把 target_pct 顶到 100，让渐变任务自己爬上去 */
        tb6612_set_dir(s_motor.dir);
    } else {
        s_motor.closed_loop = 0;
        s_motor.target_rpm = 0;
        pid_reset(&s_motor.pid);
        s_motor.target_pct = s_motor.cur_pct;
    }
}

/* ------------------------- 拓展：闭环对外接口 ------------------------- */

void motor_closed_loop_enable(uint8_t enable)
{
    if (enable) {
        s_motor.auto_ramp = 0;
        s_motor.closed_loop = 1;
        pid_reset(&s_motor.pid);
        s_motor.cl_tick = MOTOR_PID_DIV - 1;        /* 下一拍立刻采样 */
        s_motor.telem_div = MOTOR_PID_TELEM_DIV;
        s_motor.run = (s_motor.target_rpm > 0) ? 1 : 0;
        if (s_motor.run) {
            tb6612_set_dir(s_motor.dir);
        }
    } else {
        s_motor.closed_loop = 0;
        s_motor.target_rpm = 0;
        pid_reset(&s_motor.pid);
        s_motor.target_pct = s_motor.cur_pct;   /* 保持当前占空比，退出时不跳变 */
    }
}

void motor_set_target_rpm(uint16_t rpm)
{
    if (rpm > MOTOR_RPM_MAX) {
        rpm = MOTOR_RPM_MAX;
    }
    s_motor.auto_ramp = 0;
    s_motor.closed_loop = 1;
    s_motor.target_rpm = rpm;
    s_motor.run = (rpm > 0) ? 1 : 0;
    s_motor.cl_tick = MOTOR_PID_DIV - 1;
    s_motor.telem_div = MOTOR_PID_TELEM_DIV;    /* 立刻回一行，别让用户以为没反应 */
    pid_reset(&s_motor.pid);
    if (s_motor.run) {
        tb6612_set_dir(s_motor.dir);
    }
}

uint16_t motor_get_target_rpm(void)  { return s_motor.target_rpm; }
float    motor_get_rpm(void)         { return s_motor.rpm; }
uint8_t  motor_is_closed_loop(void)  { return s_motor.closed_loop; }

/* 在线整定：只换增益，不清积分，保证整定过程中输出不跳变 */
void motor_pid_set_gains(float kp, float ki, float kd)
{
    pid_set_gains(&s_motor.pid, kp, ki, kd);
}

/* ------------------------------ 周期任务 ------------------------------ */

void motor_ramp_task(void *arg)
{
    (void)arg;

    while (1) {
        /* 唯一的时间基准，没有任何阻塞延时写死别的功能 */
        vTaskDelay(pdMS_TO_TICKS(MOTOR_RAMP_PERIOD_MS));

        if (s_enc_watch_ms > 0) {
            if (++s_enc_watch_tick >= MOTOR_ENC_WATCH_DIV) {
                s_enc_watch_tick = 0;
                motor_enc_watch_print();
                s_enc_watch_ms = (s_enc_watch_ms > 200) ? (uint16_t)(s_enc_watch_ms - 200) : 0;
                if (s_enc_watch_ms == 0) {
                    uart_send("[ENCDBG] watch end\r\n");
                }
            }
        }

        if (s_motor.closed_loop) {
            /* 每 2 拍（40ms）测一次速、跑一次 PID */
            if (++s_motor.cl_tick >= MOTOR_PID_DIV) {
                s_motor.cl_tick = 0;
                encoder_sample(&s_motor.enc);
                s_motor.rpm = s_motor.enc.rpm;
                motor_closed_loop_step();
            }
            continue;               /* 闭环时占空比归 PID 独占 */
        }

        /* 开环也照样测速：这样 SPEED 之后用 RPM / STATUS 就能单独验证编码器 */
        if (++s_motor.cl_tick >= MOTOR_PID_DIV) {
            s_motor.cl_tick = 0;
            encoder_sample(&s_motor.enc);
            s_motor.rpm = s_motor.enc.rpm;
        }

        /* 自动渐变：目标在 0~100 之间折返 -> 慢->中->快->中->慢 */
        if (s_motor.auto_ramp) {
            if (s_motor.ramp_reversed) {
                s_motor.target_pct = (s_motor.target_pct >= MOTOR_RAMP_STEP_PCT)
                                     ? (uint8_t)(s_motor.target_pct - MOTOR_RAMP_STEP_PCT) : 0;
                if (s_motor.target_pct == 0) {
                    s_motor.ramp_reversed = 0;
                    s_motor.dir = (s_motor.dir == MOTOR_DIR_FWD) ? MOTOR_DIR_REV : MOTOR_DIR_FWD;
                    tb6612_set_dir(s_motor.dir);
                }
            } else {
                s_motor.target_pct = (s_motor.target_pct <= 100 - MOTOR_RAMP_STEP_PCT)
                                     ? (uint8_t)(s_motor.target_pct + MOTOR_RAMP_STEP_PCT) : 100;
                if (s_motor.target_pct == 100) {
                    s_motor.ramp_reversed = 1;
                }
            }
            s_motor.run = 1;
        }

        if (!s_motor.auto_ramp && s_motor.target_pct == 0 && s_motor.cur_pct == 0) {
            s_motor.run = 0;
        }

        /* 逐步逼近目标占空比：这一行就是"无级调速"的执行点 */
        if (s_motor.cur_pct < s_motor.target_pct) {
            uint8_t next = (uint8_t)(s_motor.cur_pct + MOTOR_RAMP_STEP_PCT);
            motor_apply_duty((next > s_motor.target_pct) ? s_motor.target_pct : next);
        } else if (s_motor.cur_pct > s_motor.target_pct) {
            uint8_t next = (s_motor.cur_pct >= MOTOR_RAMP_STEP_PCT)
                           ? (uint8_t)(s_motor.cur_pct - MOTOR_RAMP_STEP_PCT) : 0;
            motor_apply_duty((next < s_motor.target_pct) ? s_motor.target_pct : next);
        }

        /* 停下来了：PWM 拉零 + 短刹车，防止电机被惯性拖着转 */
        if (s_motor.cur_pct == 0 && s_motor.target_pct == 0 && !s_motor.auto_ramp) {
            tb6612_brake();
        }
    }
}

uint8_t     motor_get_speed(void)   { return s_motor.cur_pct; }
uint8_t     motor_get_target(void)  { return s_motor.target_pct; }
motor_dir_t motor_get_dir(void)     { return s_motor.dir; }
uint8_t     motor_is_running(void)  { return s_motor.run; }
uint8_t     motor_is_auto(void)     { return s_motor.auto_ramp; }
