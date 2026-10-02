#include "cmd.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "encoder.h"
#include "motor.h"
#include "uart.h"

/* 指令类型 */
typedef enum {
    CMD_NONE = 0,
    CMD_ON,
    CMD_OFF,
    CMD_SPEED,
    CMD_DIR,
    CMD_AUTO,
    CMD_MANUAL,
    CMD_STATUS,
    CMD_HELP,
    /* ---- 拓展：编码器测速 + PID 速度闭环 ---- */
    CMD_SET_RPM,
    CMD_RPM,
    CMD_PID,
    CMD_OPEN,
    CMD_ENCCAL,
    CMD_ENCDBG,
} cmd_id_t;

typedef enum {
    DIR_ARG_FWD = 0,
    DIR_ARG_REV,
    DIR_ARG_BAD,
} dir_arg_t;

/* 解析结果：把"这行是什么指令、参数是什么"和"怎么执行"分开 */
typedef struct {
    cmd_id_t  id;
    uint8_t   speed_pct;
    dir_arg_t dir;
    uint16_t  rpm;              /* SET_RPM 的目标转速 */
    float     kp, ki, kd;       /* PID 三个参数 */
} cmd_parsed_t;

/* ------------------------------ 通用解析工具 ------------------------------ */

/* 去掉首尾空白 */
static void trim(char *s)
{
    char *p = s;
    size_t n;

    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }

    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
}

/* 转大写，方便统一比较 */
static void upper(char *s)
{
    for (; *s != '\0'; s++) {
        *s = (char)toupper((unsigned char)*s);
    }
}

char *cmd_split(char *line, char **arg)
{
    char *cmd;
    char *sp;

    if (arg != NULL) {
        *arg = NULL;
    }
    if (line == NULL) {
        return NULL;
    }

    trim(line);
    if (line[0] == '\0') {
        return line;
    }
    upper(line);

    cmd = line;
    sp = strchr(line, ' ');
    if (sp == NULL) {
        sp = strchr(line, '\t');
    }
    if (sp != NULL) {
        *sp++ = '\0';
        trim(sp);
        if (arg != NULL) {
            *arg = (sp[0] == '\0') ? NULL : sp;   /* "ON " 这种尾随空格不算参数 */
        }
    }

    return cmd;
}

/* "25" / "+25" 这类纯数字串 -> true 并输出数值 */
uint8_t cmd_is_number(const char *s, long *out)
{
    char *end = NULL;
    long  v;

    if (s == NULL || *s == '\0') {
        return 0;
    }
    v = strtol(s, &end, 10);
    if (end == NULL || *end != '\0') {
        return 0;             /* "12abc" 这种半数字半字符一律算格式错误 */
    }
    *out = v;
    return 1;
}

/* 跳过空格 */
static const char *skip_space(const char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

/* "0.35 0.6 0" -> 三个 float；少参数、多字符、非数字都算格式错误 */
static uint8_t parse_three_floats(const char *s, float *f1, float *f2, float *f3)
{
    char *end = NULL;
    float v1, v2, v3;

    if (s == NULL) {
        return 0;
    }

    v1 = strtof(s, &end);
    if (end == s) {
        return 0;
    }
    end = (char *)skip_space(end);
    if (*end == '\0') {
        return 0;                       /* 只给了一个参数 */
    }

    v2 = strtof(end, &end);
    if (end == NULL) {
        return 0;
    }
    end = (char *)skip_space(end);
    if (*end == '\0') {
        return 0;                       /* 只给了两个参数 */
    }

    v3 = strtof(end, &end);
    if (end == NULL) {
        return 0;
    }
    end = (char *)skip_space(end);
    if (*end != '\0') {
        return 0;                       /* 后面还有垃圾字符 */
    }

    *f1 = v1;
    *f2 = v2;
    *f3 = v3;
    return 1;
}

/* PID 增益合法性：必须有限且在 0~100 之间（NaN 会在第一个比较里就被排除） */
static uint8_t gain_ok(float v)
{
    return (v >= 0.0f) && (v <= 100.0f);
}

static uint8_t parse_line(char *line, cmd_parsed_t *out)
{
    char *cmd;
    char *arg;

    memset(out, 0, sizeof(*out));

    cmd = cmd_split(line, &arg);
    if (cmd == NULL || cmd[0] == '\0') {
        return 0;                    /* 空行：静默忽略 */
    }

    if (strcmp(cmd, "ON") == 0) {
        if (arg != NULL) {
            return 0;
        }
        out->id = CMD_ON;
    } else if (strcmp(cmd, "OFF") == 0) {
        if (arg != NULL) {
            return 0;
        }
        out->id = CMD_OFF;
    } else if (strcmp(cmd, "SPEED") == 0) {
        long v;
        if (!cmd_is_number(arg, &v) || v < 0 || v > 100) {
            return 0;                /* 缺参数 / 非数字 / 越界 -> ERROR */
        }
        out->id = CMD_SPEED;
        out->speed_pct = (uint8_t)v;
    } else if (strcmp(cmd, "DIR") == 0) {
        if (arg == NULL) {
            return 0;
        }
        if (strcmp(arg, "F") == 0 || strcmp(arg, "FWD") == 0) {
            out->dir = DIR_ARG_FWD;
        } else if (strcmp(arg, "R") == 0 || strcmp(arg, "REV") == 0) {
            out->dir = DIR_ARG_REV;
        } else {
            out->dir = DIR_ARG_BAD;
            return 0;
        }
        out->id = CMD_DIR;
    } else if (strcmp(cmd, "AUTO") == 0) {
        if (arg != NULL) {
            return 0;
        }
        out->id = CMD_AUTO;
    } else if (strcmp(cmd, "MANUAL") == 0) {
        if (arg != NULL) {
            return 0;
        }
        out->id = CMD_MANUAL;
    } else if (strcmp(cmd, "STATUS") == 0 || strcmp(cmd, "?") == 0) {
        out->id = CMD_STATUS;
    } else if (strcmp(cmd, "HELP") == 0) {
        out->id = CMD_HELP;

    /* ---------------------- 拓展：PID 速度闭环指令 ---------------------- */
    } else if (strcmp(cmd, "SET_RPM") == 0 || strcmp(cmd, "SETRPM") == 0) {
        long v;
        if (!cmd_is_number(arg, &v) || v < 0 || v > MOTOR_RPM_MAX) {
            return 0;                /* 缺参数 / 非数字 / 超上限 -> ERROR */
        }
        out->id = CMD_SET_RPM;
        out->rpm = (uint16_t)v;
    } else if (strcmp(cmd, "RPM") == 0) {
        if (arg != NULL) {
            return 0;
        }
        out->id = CMD_RPM;
    } else if (strcmp(cmd, "PID") == 0) {
        float kp, ki, kd;
        if (!parse_three_floats(arg, &kp, &ki, &kd)) {
            return 0;                /* 必须给三个参数：PID <kp> <ki> <kd> */
        }
        if (!gain_ok(kp) || !gain_ok(ki) || !gain_ok(kd)) {
            return 0;                /* 负数 / 超大 / NaN / inf -> ERROR */
        }
        out->id = CMD_PID;
        out->kp = kp;
        out->ki = ki;
        out->kd = kd;
    } else if (strcmp(cmd, "OPEN") == 0) {
        if (arg != NULL) {
            return 0;
        }
        out->id = CMD_OPEN;
    } else if (strcmp(cmd, "ENCCAL") == 0) {
        if (arg != NULL) {
            return 0;
        }
        out->id = CMD_ENCCAL;
    } else if (strcmp(cmd, "ENCDBG") == 0) {
        if (arg != NULL) {
            return 0;
        }
        out->id = CMD_ENCDBG;

    } else {
        return 0;                    /* 未知指令 */
    }

    return 1;
}

void cmd_print_status(void)
{
    char line[192];

    snprintf(line, sizeof(line),
             "[MOTOR] run=%s auto=%s cl=%s dir=%s duty=%" PRIu8 "%% target=%" PRIu8 "%%"
             " rpm=%.0f trpm=%u\r\n",
             motor_is_running() ? "ON" : "OFF",
             motor_is_auto() ? "ON" : "OFF",
             motor_is_closed_loop() ? "ON" : "OFF",
             (motor_get_dir() == MOTOR_DIR_FWD) ? "F" : "R",
             motor_get_speed(),
             motor_get_target(),
             (double)motor_get_rpm(),
             (unsigned)motor_get_target_rpm());
    uart_send(line);
}

/* 闭环的周期回报与 RPM 指令共用同一种格式，方便直接对比 */
static void print_rpm(void)
{
    char line[128];

    snprintf(line, sizeof(line), "[RPM] target=%u actual=%.0f duty=%" PRIu8 "%% cl=%s\r\n",
             (unsigned)motor_get_target_rpm(),
             (double)motor_get_rpm(),
             motor_get_speed(),
             motor_is_closed_loop() ? "ON" : "OFF");
    uart_send(line);
}

void cmd_print_help(void)
{
    uart_send("---- TASK5 电机指令 ----\r\n"
              " ON               启动电机\r\n"
              " OFF              停止电机\r\n"
              " SPEED <0-100>    设置转速（占空比百分比）\r\n"
              " DIR F | DIR R    正转 / 反转\r\n"
              " AUTO             自动渐变循环（慢-中-快-中-慢）\r\n"
              " MANUAL           退出自动渐变\r\n"
              " STATUS           打印当前状态\r\n"
              " HELP             打印本表\r\n"
              " ---- 拓展：编码器 + PID ----\r\n"
              " SET_RPM <0-600>  设定目标转速并进入 PID 闭环\r\n"
              " RPM              打印一次目标 / 实测转速\r\n"
              " PID <kp> <ki> <kd>  在线整定，例如 PID 0.35 0.6 0\r\n"
              " OPEN             退出闭环（保持当前占空比）\r\n"
              " ENCCAL           打印编码器累计脉冲 / A,B 电平 / 采样数 / 错误码\r\n"
              " ENCDBG           连续 6s 观察编码器（手转轮子排障）\r\n"
              " MENU             返回主菜单\r\n"
              "------------------------\r\n");
}

/* 执行 + 回执：每条指令都必须给一个明确的 OK / ERROR */
static void cmd_execute(const cmd_parsed_t *c)
{
    switch (c->id) {
    case CMD_ON:
        motor_start();
        uart_printf("OK: motor ON, dir=%s, target=%" PRIu8 "%%\r\n",
                    (motor_get_dir() == MOTOR_DIR_FWD) ? "F" : "R",
                    motor_get_target());
        break;

    case CMD_OFF:
        motor_stop();
        uart_send("OK: motor OFF\r\n");
        break;

    case CMD_SPEED:
        motor_set_speed(c->speed_pct);
        uart_printf("OK: speed=%" PRIu8 "%%\r\n", c->speed_pct);
        break;

    case CMD_DIR:
        motor_set_dir((c->dir == DIR_ARG_FWD) ? MOTOR_DIR_FWD : MOTOR_DIR_REV);
        uart_printf("OK: dir=%s\r\n", (c->dir == DIR_ARG_FWD) ? "F" : "R");
        break;

    case CMD_AUTO:
        motor_ramp_enable(1);
        uart_send("OK: auto ramp ON\r\n");
        break;

    case CMD_MANUAL:
        motor_ramp_enable(0);
        uart_send("OK: auto ramp OFF\r\n");
        break;

    case CMD_STATUS:
        cmd_print_status();
        break;

    case CMD_HELP:
        cmd_print_help();
        break;

    /* ---------------------- 拓展：PID 速度闭环指令 ---------------------- */
    case CMD_SET_RPM:
        motor_set_target_rpm(c->rpm);
        uart_printf("OK: SET_RPM target=%u rpm, PID closed loop ON\r\n", (unsigned)c->rpm);
        break;

    case CMD_RPM:
        print_rpm();
        break;

    case CMD_PID:
        motor_pid_set_gains(c->kp, c->ki, c->kd);
        uart_printf("OK: PID kp=%.2f ki=%.2f kd=%.2f\r\n",
                    (double)c->kp, (double)c->ki, (double)c->kd);
        break;

    case CMD_OPEN:
        motor_closed_loop_enable(0);
        uart_printf("OK: closed loop OFF, duty=%" PRIu8 "%%\r\n", motor_get_speed());
        break;

    case CMD_ENCCAL:
        /* 顺带打 A/B 电平、采样次数、PCNT 错误码：
         * "pulses 一直是 0" 时用来区分"没信号"和"读计数失败" */
        {
            int a = 0;
            int b = 0;
            encoder_get_levels(&a, &b);
            uart_printf("[ENCCAL] pulses=%" PRId32 " counts_per_rev=%d A=%d B=%d samples=%u err=%d\r\n",
                        encoder_get_total(), ENCODER_COUNTS_PER_REV, a, b,
                        (unsigned)encoder_get_sample_count(), encoder_get_last_error());
        }
        break;

    case CMD_ENCDBG:
        motor_enc_watch_start(MOTOR_ENC_WATCH_MS);
        uart_printf("OK: encoder watch %u ms -- turn the wheel by hand now\r\n",
                    (unsigned)MOTOR_ENC_WATCH_MS);
        uart_send("     A/B 必须翻转、total 必须增长；否则是接线/供电问题\r\n");
        break;

    default:
        uart_send("ERROR\r\n");
        break;
    }
}

void cmd_handle_line(const char *line)
{
    char         buf[UART_CMD_LINE_MAX];
    cmd_parsed_t parsed;

    if (line == NULL) {
        return;
    }

    /* 解析会就地修改字符串，先拷一份 */
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    if (!parse_line(buf, &parsed)) {
        if (buf[0] == '\0') {
            return;                 /* 空行静默忽略 */
        }
        /* 容错要求：格式错误、参数越界、未知指令统一回 ERROR */
        uart_send("ERROR\r\n");
        return;
    }

    cmd_execute(&parsed);
}
