[bits 32]
; 若在相关的异常中 CPU 已经自动压入了错误码，为保持栈中格式统一，这里不做操作
%define ERROR_CODE nop 
; 若在相关的异常中 CPU 没有压入错误码, 为了统一校中格式，就手工压入一个。
%define ZERO push 0

extern put_str    ; 声明外部函数
extern idt_table  ; idt_table 是 C 中注册的中断处理程序数组
 
section .data
;intr_str db "interrupt occur!", 0xa, 0  ; "interrupt occur!换行0"
global intr_entry_table
intr_entry_table:

%macro VECTOR 2
section .text
intr%1entry:
    %2
    ; 保护进程上下文环境
    push ds  ; 占4字节，位于其他寄存器和内存中的值若为 16 位，入栈后只占 2 字节
    push es
    push fs
    push gs
    pushad

    ; push intr_str
    ; call put_str
    ; add esp, 4    ; 跳过压入栈的参数intr_str

    mov al, 0x20  ; 中断结束命令 EOI
    out 0xa0, al  ; 向从片发送
    out 0x20, al  ; 向主片发送

    push %1
    call [idt_table + %1*4]
    jmp intr_exit

section .data
    dd intr%1entry  ; 存储各个中断入口程序的地址, 形成 intr_entry_table 数组
    ; 编译之后，所有中断处理程序的地址都会作为数组 intr_entry_table 的元素紧凑地排在一起。
%endmacro

section .text
global intr_exit
intr_exit:
    add esp, 4    ; 跳过中断号
    popad         ; esp弹出时会被忽略
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 4    ; 跨过 error_code
    iretd         ; 从中断返回， 32位下iret等同指令iretd

VECTOR 0x00, ZERO  ; 0x00:中断向量号
VECTOR 0x01, ZERO
VECTOR 0x02, ZERO
VECTOR 0x03, ZERO
VECTOR 0x04, ZERO
VECTOR 0x05, ZERO
VECTOR 0x06, ZERO
VECTOR 0x07, ZERO
VECTOR 0x08, ERROR_CODE
VECTOR 0x09, ZERO
VECTOR 0x0a, ERROR_CODE
VECTOR 0x0b, ERROR_CODE
VECTOR 0x0c, ZERO
VECTOR 0x0d, ERROR_CODE
VECTOR 0x0e, ERROR_CODE
VECTOR 0x0f, ZERO
VECTOR 0x10, ZERO
VECTOR 0x11, ERROR_CODE
VECTOR 0x12, ZERO
VECTOR 0x13, ZERO
VECTOR 0x14, ZERO
VECTOR 0x15, ZERO
VECTOR 0x16, ZERO
VECTOR 0x17, ZERO
VECTOR 0x18, ERROR_CODE
VECTOR 0x19, ZERO
VECTOR 0x1a, ERROR_CODE
VECTOR 0x1b, ERROR_CODE
VECTOR 0x1c, ZERO
VECTOR 0x1d, ERROR_CODE
VECTOR 0x1e, ERROR_CODE
VECTOR 0x1f, ZERO  ; 31号中断，0~31已经被占用或保留
VECTOR 0x20, ZERO  ; 32号中断，时钟中断对应的入口
VECTOR 0x21, ZERO  ; 键盘中断
VECTOR 0x22, ZERO  ; 级联用的
VECTOR 0x23, ZERO  ; 串口2
VECTOR 0x24, ZERO  ; 串口1
VECTOR 0x25, ZERO  ; 并口2
VECTOR 0x26, ZERO  ; 软盘
VECTOR 0x27, ZERO  ; 并口1
VECTOR 0x28, ZERO  ; 实时时钟
VECTOR 0x29, ZERO  ; 重定向
VECTOR 0x2a, ZERO  ; 保留
VECTOR 0x2b, ZERO  ; 保留
VECTOR 0x2c, ZERO  ; ps/2鼠标
VECTOR 0x2d, ZERO  ; fpu浮点单元异常
VECTOR 0x2e, ZERO  ; 硬盘
VECTOR 0x2f, ZERO  ; 保留


;;;;;;;;;;;;;;;;   0x80号中断   ;;;;;;;;;;;;;;;;
[bits 32]
extern syscall_table
section .text
global syscall_handler
syscall_handler:
;1 保存上下文环境
   push 0			    ; 压入0, 使栈中格式统一

   push ds
   push es
   push fs
   push gs
   pushad			    ; PUSHAD指令压入32位寄存器，其入栈顺序是:
				    ; EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI 
				 
   push 0x80			    ; 此位置压入0x80也是为了保持统一的栈格式

;2 为系统调用子功能传入参数
   push edx			    ; 系统调用中第3个参数
   push ecx			    ; 系统调用中第2个参数
   push ebx			    ; 系统调用中第1个参数

;3 调用子功能处理函数
   call [syscall_table + eax*4]	    ; 编译器会在栈中根据C函数声明匹配正确数量的参数
   add esp, 12			    ; 跨过上面的三个参数

;4 将call调用后的返回值存入当前内核栈中eax的位置
   mov [esp + 8*4], eax	
   jmp intr_exit		    ; intr_exit返回,恢复上下文