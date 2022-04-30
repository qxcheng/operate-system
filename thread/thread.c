#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"
#include "kernel/print.h"
#include "memory.h"
#include "process.h"
#include "kernel/list.h"
#include "sync.h"
#include "stdio.h"
#include "file.h"
#include "fs.h"

/* pid的位图,最大支持1024个pid */
uint8_t pid_bitmap_bits[128] = {0};

/* pid池 */
struct pid_pool {
    struct bitmap pid_bitmap;  // pid位图
    uint32_t pid_start;	       // 起始pid
    struct lock pid_lock;      // 分配pid锁
}pid_pool;

struct task_struct* main_thread;
struct task_struct* idle_thread;  // idle线程
struct list thread_ready_list;
struct list thread_all_list;
static struct list_elem* thread_tag;

//struct lock pid_lock;

extern void switch_to(struct task_struct* cur, struct task_struct* next);
extern void init(void);

/* 初始化pid池 */
static void pid_pool_init(void) { 
    pid_pool.pid_start = 1;
    pid_pool.pid_bitmap.bits = pid_bitmap_bits;
    pid_pool.pid_bitmap.btmp_bytes_len = 128;
    bitmap_init(&pid_pool.pid_bitmap);
    lock_init(&pid_pool.pid_lock);
}

/* 分配pid */
static pid_t allocate_pid(void) {
    lock_acquire(&pid_pool.pid_lock);
    int32_t bit_idx = bitmap_scan(&pid_pool.pid_bitmap, 1);
    bitmap_set(&pid_pool.pid_bitmap, bit_idx, 1);
    lock_release(&pid_pool.pid_lock);
    return (bit_idx + pid_pool.pid_start);
}

/* 释放pid */
void release_pid(pid_t pid) {
    lock_acquire(&pid_pool.pid_lock);
    int32_t bit_idx = pid - pid_pool.pid_start;
    bitmap_set(&pid_pool.pid_bitmap, bit_idx, 0);
    lock_release(&pid_pool.pid_lock);
}

/* fork进程时为其分配pid,因为allocate_pid已经是静态的,别的文件无法调用.
   不想改变函数定义了,故定义fork_pid函数来封装一下。*/
pid_t fork_pid(void) {
    return allocate_pid();
}

/* 获取当前线程 pcb 指针 */
struct task_struct* running_thread() {
    uint32_t esp;
    asm ("mov %%esp, %0" : "=g"(esp));
    // 取 esp 高20位，即 pcb 起始地址
    return (struct task_struct*)(esp & 0xfffff000);
}

/* 由 kernel_thread 去执行 function(func_arg) */
static void kernel_thread(thread_func* function, void* func_arg) {
    //执行 function 前要开中断，
    //避免后面的时钟中断被屏蔽，而无法调度其他线程
    intr_enable();
    function(func_arg);
}

/* 初始化线程栈 thread_stack,
   将待执行的函数和参数放到 thread_stack 中相应的位置 */
void thread_create(struct task_struct* pthread, thread_func function, void* func_arg) {
    // 先预留中断使用栈的空间
    // 将来线程进入中断后，位于 kernel.S 中的中断代码会通过此栈来保存上下文
    // 将来实现用户进程时，会将用户进程的初始信息放在中断栈中
    pthread->self_kstack -= sizeof(struct intr_stack);
    // 再留出线程栈空间
    pthread->self_kstack -= sizeof(struct thread_stack);

    struct thread_stack* kthread_stack = (struct thread_stack*)pthread->self_kstack;
    kthread_stack->eip = kernel_thread;
    kthread_stack->function = function;
    kthread_stack->func_arg = func_arg;
    kthread_stack->ebp = kthread_stack->ebx = \
    kthread_stack->esi = kthread_stack->edi = 0;
}

/* 初始化线程基本信息 */
void init_thread(struct task_struct* pthread, char* name, int prio) {
    memset(pthread, 0, sizeof(*pthread));
    pthread->pid = allocate_pid();
    strcpy(pthread->name, name);

    // 由于把 main 函数也封装成一个线程，
    // 并且它一直是运行的，故将其直接设为 TASK_RUNNING
    if (pthread == main_thread) {
        pthread->status = TASK_RUNNING;
    } else {
        pthread->status = TASK_READY;
    }

    // self_kstack 是线程自己在内核态下使用的栈顶地址 
    pthread->self_kstack = (uint32_t*)((uint32_t)pthread + PG_SIZE);
    pthread->priority = prio;
    pthread->ticks = prio;
    pthread->elapsed_ticks = 0;
    pthread->pgdir = NULL;
    pthread->cwd_inode_nr = 0;          // 以根目录作为默认工作路径
    pthread->parent_pid = -1;            // -1表示没有父进程
    pthread->stack_magic = 0x19870916;  // 自定义的魔数, 防止入栈擦写了低处的PCB数据

    // 预留标准输入输出
    pthread->fd_table[0] = 0;
    pthread->fd_table[1] = 1;
    pthread->fd_table[2] = 2;
    // 其余的全置为 -1
    uint8_t fd_idx = 3;
    while (fd_idx < MAX_FILES_OPEN_PER_PROC) {
        pthread->fd_table[fd_idx] = -1;
        fd_idx++;
    }
}

/* 创建一优先级为 prio 的线程，线程名为 name, 线程所执行的函数是 function(func_arg) */
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg) {
    // pcb 都位于内核空间，包括用户进程的 pcb 也是在内核空间
    struct task_struct* thread = get_kernel_pages(1);

    init_thread(thread, name, prio);
    thread_create(thread, function, func_arg);

    // 确保之前不在队列中
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    // 加入就绪线程队列
    list_append(&thread_ready_list, &thread->general_tag);

    // 确保之前不在队列中
    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    // 加入全部线程队列
    list_append(&thread_all_list, &thread->all_list_tag);

    /* ret 会把栈顶的数据作为返回地址送上处理器的EIP寄存器，此时栈顶为eip指向的kernel_thread
       在执行ret后，处理器会去执行kernel_thread 函数，栈顶依次是占位的返回地址、参数1、2
       在执行完这句汇编后，线程就会开始执行
    */
    //asm volatile ("movl %0, %%esp; pop %%ebp; pop %%ebx; pop %%edi; pop %%esi; ret"::"g"(thread->self_kstack):"memory");
    
    return thread;
}

/* 将 kernel 中的 main 函数完善为主线程 */
static void make_main_thread(void) {
    /* 
    因为 main 线程早已运行，咱们在 loader.S 中进入内核时的 mov esp,0xc009f000,
    就是为其预留 pcb 的，因此 pcb 地址为 0xc009e000，不需要通过get_kernel_page另分配一页 
    */
    main_thread = running_thread();
    init_thread(main_thread, "main", 31);

    /* main 函数是当前线程，当前线程不在 thread_ready_list 中，
    所以只将其加在 thread_all_list 中 */
    ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
    list_append(&thread_all_list, &main_thread->all_list_tag);
}

/* 实现任务调度 */
void schedule() {
    ASSERT(intr_get_status() == INTR_OFF);

    struct task_struct* cur = running_thread();
    if (cur->status == TASK_RUNNING) {
        // 若此线程只是 cpu 时间片到了，将其加入到就绪队列尾
        ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
        list_append(&thread_ready_list, &cur->general_tag);
        // 重新将当前线程的 ticks 重置为其 priority
        cur->ticks = cur->priority;
        cur->status = TASK_READY;
    } else {
        // 若此线程需要某事件发生后才能继续上 cpu 运行，
        // 不需要将其加入队列，因为当前线程不在就绪队列中
    }

    /* 如果就绪队列中没有可运行的任务,就唤醒idle */
    if (list_empty(&thread_ready_list)) {
        thread_unblock(idle_thread);
    }

    ASSERT(!list_empty(&thread_ready_list));
    thread_tag = NULL;
    // 将 thread_ready_list 队列中的第一个就绪线程弹出，准备将其调度上 cpu 
    thread_tag = list_pop(&thread_ready_list);
    struct task_struct* next = elem2entry(struct task_struct, general_tag, thread_tag);
    next->status = TASK_RUNNING;

    process_activate(next);  // 激活任务页表等

    // 将线程 cur 的上下文保护好，再将线程 next 的上下文装载到处理器
    switch_to(cur, next);
}

/* 当前线程将自己阻塞，标志其状态为 stat */
void thread_block(enum task_status stat) {
    // stat 取值为 TASK_BLOCKED、TASK_WAITING、TASK_HANGING 这三种状态时才不会被调度 */
    ASSERT(((stat == TASK_BLOCKED) || (stat == TASK_WAITING) || (stat == TASK_HANGING)));
    enum intr_status old_status = intr_disable();
    struct task_struct* cur_thread = running_thread();
    cur_thread->status = stat;  // 置其状态为 stat
    schedule();  // 将当前线程换下处理器
    intr_set_status(old_status);  // 待当前线程被解除阻塞后才继续运行下面的 intr_set_status
}

/* 将线程 pthread 解除阻塞 */
// 参数 pthread 指向的是目前已经被阻塞，又希望被唤醒的线程 
// 函数 thread_unblock 是由当前运行的线程调用的，由它实施唤醒动作
void thread_unblock(struct task_struct* pthread) {
    enum intr_status old_status = intr_disable();
    ASSERT(((pthread->status == TASK_BLOCKED) || (pthread->status == TASK_WAITING) || (pthread->status == TASK_HANGING)));
    if (pthread->status != TASK_READY) {
        ASSERT(!elem_find(&thread_ready_list, &pthread->general_tag));
        if (elem_find(&thread_ready_list, &pthread->general_tag)) {
            PANIC("thread_unblock: blocked thread in ready_list\n");
        }
        // 放到队列的最前面，使其尽快得到调度
        list_push(&thread_ready_list, &pthread->general_tag);
        pthread->status = TASK_READY;
    }
    intr_set_status(old_status);
}

/* 系统空闲时运行的线程 */
static void idle(void* arg UNUSED) {
    while (1) {
        thread_block(TASK_BLOCKED);
        // 执行 hlt 时必须要保证目前处在开中断的情况下
        asm volatile ("sti; hlt":::"memory");
    }
}

/* 主动让出 cpu ，换其他线程运行 */ 
void thread_yield(void) {
    struct task_struct* cur = running_thread();
    enum intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
    list_append(&thread_ready_list, &cur->general_tag);
    cur->status = TASK_READY;
    schedule();
    intr_set_status(old_status);
}

/* 以填充空格的方式输出buf */
static void pad_print(char* buf, int32_t buf_len, void* ptr, char format) {
    memset(buf, 0, buf_len);
    uint8_t out_pad_0idx = 0;
    switch(format) {
        case 's':
            out_pad_0idx = sprintf(buf, "%s", ptr);
            break;
        case 'd':
            out_pad_0idx = sprintf(buf, "%d", *((int16_t*)ptr));
        case 'x':
            out_pad_0idx = sprintf(buf, "%x", *((uint32_t*)ptr));
    }
    while(out_pad_0idx < buf_len) { // 以空格填充
        buf[out_pad_0idx] = ' ';
        out_pad_0idx++;
    }
    sys_write(stdout_no, buf, buf_len - 1);
}

/* 用于在list_traversal函数中的回调函数,用于针对线程队列的处理 */
static bool elem2thread_info(struct list_elem* pelem, int arg UNUSED) {
    struct task_struct* pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    char out_pad[16] = {0};

    pad_print(out_pad, 16, &pthread->pid, 'd');

    if (pthread->parent_pid == -1) {
        pad_print(out_pad, 16, "NULL", 's');
    } else { 
        pad_print(out_pad, 16, &pthread->parent_pid, 'd');
    }

    switch (pthread->status) {
        case 0:
            pad_print(out_pad, 16, "RUNNING", 's');
            break;
        case 1:
            pad_print(out_pad, 16, "READY", 's');
            break;
        case 2:
            pad_print(out_pad, 16, "BLOCKED", 's');
            break;
        case 3:
            pad_print(out_pad, 16, "WAITING", 's');
            break;
        case 4:
            pad_print(out_pad, 16, "HANGING", 's');
            break;
        case 5:
            pad_print(out_pad, 16, "DIED", 's');
    }
    pad_print(out_pad, 16, &pthread->elapsed_ticks, 'x');

    memset(out_pad, 0, 16);
    ASSERT(strlen(pthread->name) < 17);
    memcpy(out_pad, pthread->name, strlen(pthread->name));
    strcat(out_pad, "\n");
    sys_write(stdout_no, out_pad, strlen(out_pad));
    return false;	// 此处返回false是为了迎合主调函数list_traversal,只有回调函数返回false时才会继续调用此函数
}

/* 打印任务列表 */
void sys_ps(void) {
   char* ps_title = "PID            PPID           STAT           TICKS          COMMAND\n";
   sys_write(stdout_no, ps_title, strlen(ps_title));
   list_traversal(&thread_all_list, elem2thread_info, 0);
}

/* 回收thread_over的pcb和页表,并将其从调度队列中去除 */
void thread_exit(struct task_struct* thread_over, bool need_schedule) {
    /* 要保证schedule在关中断情况下调用 */
    intr_disable();
    thread_over->status = TASK_DIED;

    /* 如果thread_over不是当前线程,就有可能还在就绪队列中,将其从中删除 */
    if (elem_find(&thread_ready_list, &thread_over->general_tag)) {
        list_remove(&thread_over->general_tag);
    }
    if (thread_over->pgdir) {     // 如是进程,回收进程的页表
        mfree_page(PF_KERNEL, thread_over->pgdir, 1);
    }

    /* 从all_thread_list中去掉此任务 */
    list_remove(&thread_over->all_list_tag);
    
    /* 回收pcb所在的页,主线程的pcb不在堆中,跨过 */
    if (thread_over != main_thread) {
        mfree_page(PF_KERNEL, thread_over, 1);
    }

    /* 归还pid */
    release_pid(thread_over->pid);

    /* 如果需要下一轮调度则主动调用schedule */
    if (need_schedule) {
        schedule();
        PANIC("thread_exit: should not be here\n");
    }
}

/* 比对任务的pid */
static bool pid_check(struct list_elem* pelem, int32_t pid) {
    struct task_struct* pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    if (pthread->pid == pid) {
        return true;
    }
    return false;
}

/* 根据pid找pcb,若找到则返回该pcb,否则返回NULL */
struct task_struct* pid2thread(int32_t pid) {
    struct list_elem* pelem = list_traversal(&thread_all_list, pid_check, pid);
    if (pelem == NULL) {
        return NULL;
    }
    struct task_struct* thread = elem2entry(struct task_struct, all_list_tag, pelem);
    return thread;
}

/* 初始化线程环境 */
void thread_init(void) {
    put_str("thread_init start\n");
    list_init(&thread_ready_list);
    list_init(&thread_all_list);
    pid_pool_init();
    //lock_init(&pid_lock);

    /* 先创建第一个用户进程:init */
    process_execute(init, "init");         // 放在第一个初始化,这是第一个进程,init进程的pid为1

    make_main_thread();  // 将当前 main 函数创建为线程
    
    idle_thread = thread_start("idle", 10, idle, NULL);  // 创建 idle 线程

    put_str("thread_init done\n");
}