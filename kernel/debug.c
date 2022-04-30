# include "debug.h"
# include "kernel/print.h"
# include "interrupt.h"

/* 打印文件名、行号、函数名、条件并使程序悬停 */
void panic_spin(char* filename, int line, const char* func, const char* condition) {
    // 因为有时候会单独调用 panic_spin，所以在此处关中断
    intr_disable();

    put_str("Something wrong...");
    
    put_str("FileName: ");
    put_str(filename);
    put_char('\n');

    put_str("Line: ");
    put_int(line);
    put_char('\n');

    put_str("Function: ");
    put_str(func);
    put_char('\n');

    put_str("Condition: ");
    put_str(condition);
    put_char('\n');

    while (1);
}