#include "interrupt.h"
#include "stdint.h"
#include "global.h"
#include "io.h"
#include "kernel/print.h"

#define IDT_DESC_CNT 0x81  // 目前总共支持的中断数
#define PIC_M_CTRL 0x20   // 主片的控制端口是 0x20
#define PIC_M_DATA 0x21   // 主片的数据端口是 0x21
#define PIC_S_CTRL 0xa0   // 从片的控制端口是 0xa0
#define PIC_S_DATA 0xa1   // 从片的数据端口是 0xa1

#define EFLAGS_IF 0x00000200  // eflags 寄存器中的 if 位为 1
#define GET_EFLAGS(EFLAG_VAR) asm volatile("pushfl; popl %0":"=g"(EFLAG_VAR)) 
//用寄存器约束 g 来约束 EFLAG_VAR 可以放在内存中或寄存器中
//用 pushfl 将 eflagS 寄存器的值压入栈
//用 popl 指令将其弹出到与 EFLAG_VAR 关联的约束中
//最后 C 变量 EFLAG_VAR获得 eflags 的值

extern uint32_t syscall_handler(void);

/*中断门描述符结构体*/
struct gate_desc {
    uint16_t func_offset_low_word;
    uint16_t selector;
    uint8_t dcount;
    uint8_t attribute;
    uint16_t func_offset_high_word;
};

// 静态函数声明，非必须
static void make_idt_desc(struct gate_desc* p_gdesc, uint8_t attr, intr_handler function);
// idt 是中断描述符表, 本质是个中断门描述符数组
static struct gate_desc idt[IDT_DESC_CNT];
// 声明引用定义在 kernel.S 中的中断处理函数入口数组
extern intr_handler intr_entry_table[IDT_DESC_CNT];

char* intr_name[IDT_DESC_CNT];  // 用于保存异常的名字
// 定义中断处理程序数组，在 kernel.S 中定义的 intrXXentry
// 只是中断处理程序的入口，最终调用的是 idt_table 中的处理程序
intr_handler idt_table[IDT_DESC_CNT];  


/* 创建中断门描述符 */
static void make_idt_desc(struct gate_desc* p_gdesc, uint8_t attr, intr_handler function) {
    p_gdesc->func_offset_low_word = (uint32_t)function & 0x0000FFFF;
    p_gdesc->selector = SELECTOR_K_CODE;
    p_gdesc->dcount = 0;
    p_gdesc->attribute = attr;
    p_gdesc->func_offset_high_word = ((uint32_t)function & 0xFFFF0000) >> 16;
}

/* 初始化中断描述符表 */
static void idt_desc_init(void) {
    int i, lastindex = IDT_DESC_CNT - 1;
    for (i=0; i<IDT_DESC_CNT; i++) {
        make_idt_desc(&idt[i], IDT_DESC_ATTR_DPL0, intr_entry_table[i]);
    }
    /* 单独处理系统调用，系统调用对应的中断门 dpl 为 3,
       若指定为 0 级，则在 3 级环境下执行 int 指令会产生 GP 异常。
       中断处理程序为单独的 syscall_handler */
    make_idt_desc(&idt[lastindex], IDT_DESC_ATTR_DPL3, syscall_handler);
    put_str("idt_desc_init done\n");
}

/* 初始化可编程中断控制器 8259A */ 
static void pic_init(void) {
    // 初始化主片
    outb(PIC_M_CTRL, 0x11);  // ICW1：边沿触发，级联8259 ，需要 ICW4
    outb(PIC_M_DATA, 0x20);  // ICW2：起始中断向量号为 0x20, 也就是IR[0-7]为 0x20-0x27

    outb(PIC_M_DATA, 0x04);  // ICW3：IR2接从片
    outb(PIC_M_DATA, 0x01);  // ICW4: 8086 模式，正常 EOI

    /* 初始化从片 */
    outb(PIC_S_CTRL, 0x11);  // ICW1：边沿触发，级联8259 ，需要 ICW4
    outb(PIC_S_DATA, 0x28);  // ICW2：起始中断向量号为 0x28, 也就是IR[8-15]为 0x28-0x2F

    outb(PIC_S_DATA, 0x02);  // ICW3：设置从片连接到主片的 IR2 引脚
    outb(PIC_S_DATA, 0x01);  // ICW4: 8086 模式，正常 EOI

    /* 打开主片上 IR0 ，也就是目前只接受时钟产生的中断 */
    //outb(PIC_M_DATA, 0xfe);  // 第 0 位为 0，表示不屏蔽 IR0 的时钟中断
    //outb(PIC_M_DATA, 0xfd);  // 测试键盘，只打开键盘中断，其他全部关闭
    //outb(PIC_M_DATA, 0xfc);    // 将时钟中断和键盘中断都打开
    outb(PIC_M_DATA, 0xf8);  // IRQ2 用于级联从片，必须打开。主片上打开的中断有 IRQ0 的时钟，IRQ1 的键盘和级联从片的IRQ2
    outb(PIC_S_DATA, 0xbf);  // 打开从片上的 IRQ14 ，此引脚接收硬盘控制器的中断

    put_str("pic_init done.\n");
}

/* 通用的中断处理函数，一般用在异常出现时的处理 */
static void general_intr_handler(uint8_t vec_nr) {
    if (vec_nr == 0x27 || vec_nr == 0x2f) {
        // IRQ7 和 IRQ15 会产生伪中断（ spurious interrupt ），无需处理
        // 0x2f 是从片 8259A 上的最后一个 IRQ 引脚，保留项
        return;
    } 
    // 将光标置为 0 ，从屏幕左上角清出一片打印异常信息的区域，方便阅读
    set_cursor(0); // 范围 0 ~ 1999
    int cursor_pos = 0;
    while(cursor_pos < 320) {
        put_char(' ');
        cursor_pos++;
    }
    set_cursor(0);   // 重置光标为屏幕左上角
    put_str("! ! ! ! ! ! ! excetion message begin ! ! ! ! ! ! ! ! \n");
    set_cursor(88);  // 从第 2 行第 8 个字符开始打印
    put_str(intr_name[vec_nr]);
    // 若为 Pagefault ，将缺失的地址打印出来并悬停
    if (vec_nr == 14) {
        int page_fault_vaddr = 0;
        // cr2 是存放造成 page_fault 的地址
        asm ("movl %%cr2, %0" : "=r"(page_fault_vaddr));
        put_str("\npage fault addr is ");
        put_int(page_fault_vaddr);
    }
    put_str("\n ! ! ! ! ! ! ! excetion message end ! ! ! ! ! ! ! ! \n");
    // 能进入中断处理程序就表示已经处在关中断情况下
    // 不会出现调度进程的情况。故下面的死循环不会再被中断
    while(1);

    // put_str("int vector: 0x");
    // put_int(vec_nr);
    // put_char('\n');
}

/* 完成一般中断处理函数注册及异常名称注册 */
static void exception_init(void) {
    int i;
    for (i=0; i<IDT_DESC_CNT; i++) {
        /* idt_table 数组中的函数是在进入中断后根据中断向量号调用的
        见kernel/kernel.S 的 call [idt_table ＋%1*4] */
        idt_table[i] = general_intr_handler;  // 以后会由 register_handler 来注册具体处理函数
        intr_name[i] = "unknown";
    }
    intr_name[0] = "#DE Divide Error";
    intr_name[1] = "#DB Debug Exception";
    intr_name[2] = "NMI Interrupt";
    intr_name[3] = "#BP Breakpoint Exception";
    intr_name[4] = "#OF Overflow Exception";
    intr_name[5] = "#BR BOUND Range Exceeded Exception";
    intr_name[6] = "#UD Invalid Opcode Exception";
    intr_name[7] = "#NM Device Not Available Exception";
    intr_name[8] = "#DF Double Fault Exception";
    intr_name[9] = "Coprocessor Segment Overrun";
    intr_name[10] = "#TS Invalid TSS Exception";
    intr_name[11] = "#NP Segment Not Present";
    intr_name[12] = "#SS Stack Fault Exception";
    intr_name[13] = "#GP General Protection Exception";
    intr_name[14] = "#PF Page-Fault Exception";
    intr_name[16] = "#MF 0x87 FPU Floating-Point Error";
    intr_name[17] = "#AC Alignment Check Exception";
    intr_name[18] = "#MC Machine-Check Exception";
    intr_name[19] = "#XF SIMD Floating-Point Exception";
}


/* 完成有关中断的所有初始化工作 */
void idt_init() {
    put_str("idt_init start\n");
    idt_desc_init();   // 初始化中断描述符表
    exception_init();  // 异常名初始化并注册通常的中断处理函数
    pic_init();        // 初始化 8259A

    /* 加载 idt */
    // lidt 会取出前 48 位数据做操作数
    uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16));
    asm volatile("lidt %0"::"m"(idt_operand));  // idt_operand被当作内存寻址
    put_str("idt_init done\n");
}

/*开中断并返回开中断前的状态*/
//每个分支中都有 return 语句时，能够避免将 C 编译为汇编代码时因为
//共用一行代码而额外添加 jmp 语句，虽然程序因此而大了一点，但也因此而快了一点，空间换时间。
enum intr_status intr_enable() {
    enum intr_status old_status;
    if (INTR_ON == intr_get_status()) {
        old_status = INTR_ON;
        return old_status;
    } else {
        old_status = INTR_OFF;
        asm volatile("sti");  // 开中断， sti 指令将IF位置 1
        return old_status;
    }
}

/*关中断，并且返回关中断前的状态*/
enum intr_status intr_disable() {
    enum intr_status old_status;
    if (INTR_ON == intr_get_status()) {
        old_status = INTR_ON;
        asm volatile("cli":::"memory");  //关中断, cli 指令将 IF 位置 0
        return old_status;
    } else {
        old_status = INTR_OFF;
        return old_status;
    }
}

/*将中断状态设置为 status */
enum intr_status intr_set_status(enum intr_status status) {
    return status & INTR_ON ? intr_enable() : intr_disable();
}

/*获取当前中断状态*/
enum intr_status intr_get_status() {
    uint32_t eflags = 0;
    GET_EFLAGS(eflags);
    return (EFLAGS_IF & eflags) ? INTR_ON : INTR_OFF;
}

/* 在中断处理程序数组第 vector_no 个元素中
   注册安装中断处理程序 function */
void register_handler(uint8_t vector_no, intr_handler function) {
    // idt_table 数组中的函数是在进入中断后根据中断向量号调用的
    idt_table[vector_no] = function;
}