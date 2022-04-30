#ifndef __LIB_IO_H
#define __LIB_IO_H

#include "stdint.h"

/* 向端口 port 写入一个字节 */
static inline void outb(uint16_t port, uint8_t data) {
    // %b0 表示对应 al, %w1 表示对应 dx */
    // 对端口指定 N 表示 0~255, d 表示用 dx 存储端口号
    asm volatile("outb %b0, %w1"::"a"(data),"Nd"(port));
}

/* 将 addr 处起始的 word_cnt 个字写入端口 port */
static inline void outsw(uint16_t port, const void* addr, uint32_t word_cnt) {
    // +表示此限制即做输入，又做输出
    // outsw 是把 ds:esi 处的 16 位的内容写入 port 端口，我们在设置段描述符时，
    // 已经将ds,es,ss 段的选择子都设置为相同的值了，此时不用担心数据错乱。
    asm volatile("cld; rep outsw":"+S"(addr),"+c"(word_cnt):"d"(port));
}

/* 将从端口 port 读入的一个字节返回 */
static inline uint8_t inb(uint16_t port) {
    uint8_t data;
    asm volatile("inb %w1, %b0":"=a"(data):"Nd"(port));
    return data;
}

/* 将从端口 port 读入的 word_cnt 个字写入 addr */
static inline void insw(uint16_t port, void* addr, uint32_t word_cnt) {
    // insw 是将从端口 port 处读入的 16 位内容写入 es:edi 指向的内存，
    // 我们在设置段描述符时，已经将ds,es,ss 段的选择子都设置为相同的值了，此时不用担心数据错乱
    // "memory"内存破坏
    asm volatile("cld; rep insw":"+D"(addr),"+c"(word_cnt):"d"(port):"memory");
}

#endif