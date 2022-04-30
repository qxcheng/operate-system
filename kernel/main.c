#include "kernel/print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "ioqueue.h"
#include "keyboard.h"
#include "process.h"
#include "user/syscall.h"
#include "syscall-init.h"
#include "stdio.h"
#include "dir.h"
#include "fs.h"
#include "user/assert.h"
#include "shell.h"

void k_thread_a(void*);
void k_thread_b(void*);
void u_prog_a(void);
void u_prog_b(void);

void init(void);


int main(void) {
    // put_char('k');
    // put_char('e');
    // put_char('r');
    // put_char('n');
    // put_char('e');
    // put_char('l');
    // put_char('\n');
    // put_char('1');
    // put_char('2');
    // put_char('\b');
    // put_char('3');
    // put_char('\n');

    put_str("I am kernel!\n");
    // put_int(7);
    // put_char('\n');
    // put_int(0x7c001234);

    init_all();
    //asm volatile("sti");  // 开中断, 它将标志寄存器 eflags 中的 IF 位置 1

    //ASSERT(1==2);

    // void* addr = get_kernel_pages(3);
    // put_str("\n get_kernel_page start vaddr is ");
    // put_int((uint32_t)addr);
    // put_char('\n');

    // process_execute(u_prog_a, "user_prog_a");
    // process_execute(u_prog_b, "user_prog_b");

    // intr_enable();  // 打开中断，使时钟中断起作用

    // console_put_str(" main_pid:0x");
    // console_put_int(sys_getpid());
    // console_put_char('\n');

    // thread_start("k_thread_a", 31, k_thread_a, "ThreadA");
    // thread_start("k_thread_b", 31, k_thread_b, "ThreadB");

/********  测试代码  ********/
    // struct stat obj_stat;
    // sys_stat("/", &obj_stat);
    // printf("/`s info\n   i_no:%d\n   size:%d\n   filetype:%s\n", \
    //     obj_stat.st_ino, obj_stat.st_size, \
    //     obj_stat.st_filetype == 2 ? "directory" : "regular");
    // sys_stat("/dir1", &obj_stat);
    // printf("/dir1`s info\n   i_no:%d\n   size:%d\n   filetype:%s\n", \
    //     obj_stat.st_ino, obj_stat.st_size, \
    //     obj_stat.st_filetype == 2 ? "directory" : "regular");
/********  测试代码  ********/

/*************    写入应用程序    *************/
    // uint32_t file_size = 6952; 
    // uint32_t sec_cnt = DIV_ROUND_UP(file_size, 512);
    // struct disk* sda = &channels[0].devices[0];
    // void* prog_buf = sys_malloc(file_size);
    // ide_read(sda, 300, prog_buf, sec_cnt);
    // int32_t fd = sys_open("/prog_pipe", O_CREAT|O_RDWR);
    // if (fd != -1) {
    //     if(sys_write(fd, prog_buf, file_size) == -1) {
    //         printk("file write error!\n");
    //         while(1);
    //     }
    // }
/*************    写入应用程序结束   *************/

    cls_screen();
    console_put_str("[qxc@loacalhost /]$ ");
    thread_exit(running_thread(), true);
    //while(1);
    // while(1) {
    //     console_put_str("Main ");
    // }
    return 0;
}

/* init进程 */
void init(void) {
    uint32_t ret_pid = fork();
    if(ret_pid) {  // 父进程
        int status;
        int child_pid;
        /* init在此处不停的回收僵尸进程 */
        while(1) {
            child_pid = wait(&status);
            printf("I`m init, My pid is 1, I recieve a child, It`s pid is %d, status is %d\n", child_pid, status);
        }
    } else {	  // 子进程
        my_shell();
    }
    panic("init: should not be here");
}

/* 在线程中运行的函数 */
void k_thread_a(void* arg) {
    //用 void* 来通用表示参数，
    //被调用的函数知道自己需要什么类型的参数，自己转换再用
    void* addr1 = sys_malloc(256);
    void* addr2 = sys_malloc(255);
    void* addr3 = sys_malloc(254);
    console_put_str(" thread_a malloc addr:0x");
    console_put_int((int)addr1);
    console_put_char(',');
    console_put_int((int)addr2);
    console_put_char(',');
    console_put_int((int)addr3);
    console_put_char('\n');

    int cpu_delay = 100000;
    while(cpu_delay-- > 0);
    sys_free(addr1);
    sys_free(addr2);
    sys_free(addr3);
    while(1);
}

/* 在线程中运行的函数 */
void k_thread_b(void* arg) {
    //用 void* 来通用表示参数，
    //被调用的函数知道自己需要什么类型的参数，自己转换再用
    void* addr1 = sys_malloc(256);
    void* addr2 = sys_malloc(255);
    void* addr3 = sys_malloc(254);
    console_put_str(" thread_b malloc addr:0x");
    console_put_int((int)addr1);
    console_put_char(',');
    console_put_int((int)addr2);
    console_put_char(',');
    console_put_int((int)addr3);
    console_put_char('\n');

    int cpu_delay = 100000;
    while(cpu_delay-- > 0);
    sys_free(addr1);
    sys_free(addr2);
    sys_free(addr3);
    while(1);
}

void u_prog_a(void) {
    void* addr1 = malloc(256);
    void* addr2 = malloc(255);
    void* addr3 = malloc(254);
    printf(" prog_a malloc addr:0x%x,0x%x,0x%x\n", (int)addr1, (int)addr2, (int)addr3);

    int cpu_delay = 100000;
    while(cpu_delay-- > 0);
    free(addr1);
    free(addr2);
    free(addr3);
    while(1);
}

void u_prog_b(void) {
    void* addr1 = malloc(256);
    void* addr2 = malloc(255);
    void* addr3 = malloc(254);
    printf(" prog_b malloc addr:0x%x,0x%x,0x%x\n", (int)addr1, (int)addr2, (int)addr3);

    int cpu_delay = 100000;
    while(cpu_delay-- > 0);
    free(addr1);
    free(addr2);
    free(addr3);
    while(1);
}