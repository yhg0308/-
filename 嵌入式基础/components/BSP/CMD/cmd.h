#ifndef __CMD_H_
#define __CMD_H_

/* TASK5 电机指令解析层（指令表见 cmd_print_help() 的输出 / README） */

#include <stdint.h>

/* 解析一行指令并执行（回复通过 uart_printf 直接回写串口） */
void cmd_handle_line(const char *line);

void cmd_print_help(void);
void cmd_print_status(void);

/* 通用解析小工具，APP 的 Task1~Task4 指令表也用：
 * cmd_split 就地把一行拆成"命令 + 参数"，返回命令指针，*arg 无参数时为 NULL */
char   *cmd_split(char *line, char **arg);
uint8_t cmd_is_number(const char *s, long *out);

#endif
