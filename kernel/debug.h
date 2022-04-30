# ifndef _KERNEL_DEBUG_H
# define _KERNEL_DEBUG_H

void panic_spin(char* filename, int line, const char* func, const char* condition);

/**
 * 当断言被触发时调用.
 * __FILE__: 内置宏,表示调用的文件名
 * __LINE__: 内置宏,被编译文件的行号
 * __func__: 内置宏: 被编译的函数名
 * __VA_ARGS__: 是预处理器所支持的专用标识符，代表所有与省略号相对应的参数
 * ...表示定义的宏其参数可变。
 */ 
# define PANIC(...) panic_spin (__FILE__, __LINE__, __func__, __VA_ARGS__)

//符号＃让编译器将宏的参数转化为字符串
//于是传给 panic_spin 函数的第 4 个参数＿VA_ARGS__实际类型为字符串指针。
# ifdef NDEBUG
    # define ASSERT(CONDITION) ((void) 0)
# else
    # define ASSERT(CONDITION) \
    if (CONDITION) { \
    } else { \
        PANIC(#CONDITION); \
    }
# endif
# endif