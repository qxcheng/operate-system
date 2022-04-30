[bits 32]
section .text
global switch_to
switch_to:
    ; 栈中此处是返回地址
    push esi
    push edi
    push ebx
    push ebp

    mov eax, [esp+20]  ; 得到栈中的参数cur , cur = [esp+20]
    ; 保存栈顶指针esp. task_struct 的 self_kstack 字段
    ; self_kstack 在 task_struct 中的偏移为 0
    ; 所以直接往 thread 开头处存 4 字节便可
    mov [eax], esp

    ; 以上是备份当前线程的环境，下面是恢复下一个线程的环境
    mov eax, [esp+24]  ; 得到栈中的参数next, next = [esp+24]
    ; pcb 的第一个成员是 self_kstack 成员
    ; 它用来记录0级栈顶指针，被换上 cpu 时用来恢复0级栈
    ; 0 级栈中保存了进程或线程所有信息，包括 3 级栈指针
    mov esp, [eax]
    pop ebp
    pop ebx
    pop edi
    pop esi
    ret
    ; 返回到上面 switch_to 下面的那句注释的返回地址，
    ; 未由中断进入，第一次执行时会返回到 kernel_thread (弹出的是线程栈中初始化的4个0、eip)