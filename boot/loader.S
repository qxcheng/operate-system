%include "boot.inc"
section loader vstart=LOADER_BASE_ADDR

; 构建 gdt 及其内部的描述符
GDT_BASE: dd 0x00000000  ; 低4字节
          dd 0x00000000  ; 高4字节

CODE_DESC: dd 0x0000FFFF  ; 高位段基址是0，低位段界限全为1
           dd DESC_CODE_HIGH4

DATA_STACK_DESC: dd 0x0000FFFF
                 dd DESC_DATA_HIGH4

VIDEO_DESC: dd 0x80000007        ;limit=(0xbffff-0xb8000)/4k=0x7
            dd DESC_VIDEO_HIGH4  ;此时dpl为0

GDT_SIZE equ $ - GDT_BASE
GDT_LIMIT equ GDT_SIZE - 1
times 60 dq 0            ;此处预留 60 个描述符的空位
SELECTOR_CODE equ (0x0001<<3) + TI_GDT + RPL0  ;相当于(CODE_DESC - GDT_BASE)/8 + TI_GDT + RPL0
SELECTOR_DATA equ (0x0002<<3) + TI_GDT + RPL0
SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0

; total_mem_bytes 用于保存内存容量，以字节为单位，此位置比较好记
; 当前偏移 loader.bin 文件头 0x200 字节, loader. bin 的加载地址是 0x900
; 故 total mem_bytes 内存中的地址是 0xb00, 将来在内核中咱们会引用此地址
total_mem_bytes dd 0
;;;;;

; 以下是 gdt 的指针，前 2 字节是gdt界限，后 4 字节是 gdt 起始地址
gdt_ptr dw GDT_LIMIT
        dd GDT_BASE

; 人工对齐： total_mem_bytes4+gdt_ptr6+ards_buf244+ards_nr2 ，共 256 字节
ards_buf times 244 db 0
ards_nr dw 0              ; 用于记录 ARDS 结构体数量

loader_start:
;获取内存1 int 15h eax = 0000E820h ,edx = 534D4150h ('SMAP'）获取内存布局
    xor ebx, ebx          ; 第一次调用时，ebx 值要为0
    mov edx, 0x534d4150   ; edx 只赋值一次，循环体中不会改变
    mov di, ards_buf      ; ards 结构缓冲区
.e820_mem_get_loop:
    mov eax, 0x0000eb20   ; 执行 int 0x15 后，eax值变为 0x534d4150, 所以每次执行 int 前都要更新为子功能号
    mov ecx, 20           ; ARDS 地址范围描述符结构大小是 20 字节
    int 0x15
    jc .e820_failed_so_try_e801  ; 若cf位为 1 时有错误发生，尝试 0xe801子功能
    add di, cx            ; 使 di 增加 20 字节指向缓冲区中新的 ARDS 结构位置
    inc word [ards_nr]    ; 记录 ARDS 数量
    cmp ebx, 0
    jnz .e820_mem_get_loop  ; ebx不为0时，继续调用获取ards

    ;在所有ards结构中找出(base_addr_low + length_low)的最大值，即内存的容量
    ;0~31位+64~95位, 即0~3字节+8~11字节
    mov cx, [ards_nr]  ; 遍历每一个 ARDS 结构体，循环次数是 ARDS 的数量
    mov ebx, ards_buf
    xor edx, edx       ; edx 为最大的内存容量，在此先清0
.find_max_mem_area:
    mov eax, [ebx]     ; base_addr_low
    add eax, [ebx+8]   ; length low
    add ebx, 20        ; 指向缓冲区中下－个 ARDS 结构
    cmp edx, eax
    jge .next_ards     ; 若 edx >= eax, 则继续循环找下一个结构体
    mov edx, eax       ; 否则将 edx 更新为当前最大内存
.next_ards:
    loop .find_max_mem_area
    jmp .mem_get_ok

;获取内存2  int 15h ax = E801h 获取内存大小，最大支持 4G
;返回后， ax cx 值一样，以 KB 为单位， bx dx 值一样，以 64KB 为单位
;在 ax 和 cx 寄存器中为低16MB ，在 bx 和 dx 寄存器中为 16MB 到 4GB
.e820_failed_so_try_e801:
    mov ax, 0xe801
    int 0x15
    jc .e801_failed_so_try88  ; 若当前 e801 方法失败，就尝试 0x88 方法

    ; 1 先算出低 15MB 的内存
    ; ax 和 cx 中是以 KB 为单位的内存数量，将其转换为以 byte 为单位
    mov cx, 0x400  ; cx 和 ax 值一样， cx 用作乘数
    mul cx         ; 结果在dx:ax
    shl edx, 16    ; 结果的高位
    and eax, 0x0000FFFF  ; 结果的低位
    or edx, eax          ; 真实结果
    add edx, 0x100000    ; ax 只是 15MB ，故要加 lMB
    mov esi, edx         ; 先把低 15MB 的内存容量存入 esi 寄存器备份

    ; 2 再将 16MB 以上的内存转换为 byte 为单位
    ; 寄存器 bx 和 dx 中是以 64KB 为单位的内存数量
    xor eax, eax
    mov ax, bx
    mov ecx, 0x10000  ; 0x10000 十进制为 64KB
    mul ecx           ; 结果存在EDX:EAX
    add esi, eax      ; 由于此方法只能测出 4GB 以内的内存，故32位eax足够了, edx肯定为0 ，只加eax便可
    mov edx, esi      ; edx 为总内存大小
    jmp .mem_get_ok

;获取内存3 int 15h ah = 0x88 获取内存大小，只能获取 64MB 之内
.e801_failed_so_try88:
    mov ah, 0x88
    int 0x15
    jc .error_hlt
    and eax, 0x0000FFFF  ; int 15 后， ax 存入的是以 KB 为单位的内存容量

    mov cx, 0x400   ; 0x400 等于 1024 ，将 ax 中的内存容量换为以 byte 为单位
    mul cx          ; dx:ax
    shl edx, 16
    or edx, eax     ; 32 位的积
    add edx, 0x100000  ; 0x88 子功能只会返回 lMB 以上的内存, ；故实际内存大小要加上 lMB

.error_hlt:
    mov byte [gs:160], 'f'
    mov byte [gs:162], 'a'
    mov byte [gs:164], 'i'
    mov byte [gs:166], 'l'
    jmp $

.mem_get_ok:
    mov [total_mem_bytes], edx  ; 将内存换为 byte 单位后存入total_mem_bytes处

;准备进入保护模式
    ;1 打开A20
    in al, 0x92
    or al, 00000010b
    out 0x92, al

    ;2 加载GDT
    lgdt [gdt_ptr]

    ;3 cr0第0位置1
    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax

    jmp dword SELECTOR_CODE:p_mode_start  ; 刷新流水线

[bits 32]
p_mode_start:
    mov ax, SELECTOR_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, LOADER_STACK_TOP
    mov ax, SELECTOR_VIDEO
    mov gs, ax

    ;1 加载硬盘中的kernel.bin
    mov eax, KERNEL_START_SECTOR
    mov ebx, KERNEL_BIN_BASE_ADDR
    mov ecx, 200
    call rd_disk_m_32


    ;2 开启分页
    call setup_page  ; 开启分页第1步：创建页目录及页表并初始化页内存位图

    sgdt [gdt_ptr]   ; 将描述符表地址及偏移量写入内存gdt_ptr，一会儿用新地址重新加载

    ; 将 gdt 描述符中视频段描述符中的段基址+0xc0000000
    mov ebx, [gdt_ptr+2]  ; gdt地址
    ; 视频段是第 3 个段描述符，每个描述符是 8 字节，故 0x18
    ; 段描述符的高 4 字节的最高位是段基扯的第31~24位
    or dword [ebx + 0x18 + 4], 0xc0000000
    
    ; 将 gdt 的基址加上 0xc0000000 使其成为内核所在的高地址
    add dword [gdt_ptr + 2], 0xc0000000
    
    ; 将栈指针同样映射到内核地址
    add esp, 0xc0000000 
    
    ; 开启分页第2步：把页目录地址赋给 cr3
    mov eax, PAGE_DIR_TABLE_POS
    mov cr3, eax
    
    ; 开启分页第3步：打开 cr0 的 pg 位（第 31 位）
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    
    ; 在开启分页后，用 gdt 新的地址重新加载
    lgdt [gdt_ptr]

    ; 在分页后， GDT 的基址会变成 3GB 之上的虚拟地址，显存段基址也变成了 3GB 之上的虚拟地址
    mov byte [gs:160], 'Q'  ; 视频段段基址已经被更新

    ; 3 加载内核
    ; 此时不刷新流水线也没问题
    ; 由于一直处在 32 位下，原则上不需要强制刷新, 经过实际测试没有以下这两句也没问题
    ; 但以防万一，还是加上啦，免得将来出来莫名其妙的问题
    jmp SELECTOR_CODE:enter_kernel  ; 强制刷新流水线，更新 gdt
enter_kernel:   
    call kernel_init
    mov esp, 0xc009f000
    jmp KERNEL_ENTRY_POINT  ; 用地址 0x1500 访问测试，结果 ok

    jmp $

;1 函数：创建页目录表及页表
setup_page:
    ; 先把页目录表占用的空间逐字节清 0
    mov ecx, 4096
    mov esi, 0
.clear_page_dir:
    mov byte [PAGE_DIR_TABLE_POS + esi], 0
    inc esi
    loop .clear_page_dir

; 开始创建页目录项(PDE)
.create_pde:
    mov eax, PAGE_DIR_TABLE_POS
    add eax, 0x1000  ; 0x1000=1024x4=4096, 此时 eax 为第一个页表的位置及属性
    mov ebx, eax     ; 为 .create_pte 做准备

    ; 1
    ; 在加载内核之前，运行的 loader 在1MB之内，必须保证之前段机制下的线性地址和分页
    ; 后的虚拟地址对应的物理地址一致。
    ; 第 0 个页目录项代表的页表，其表示的空间是 0~0x3fffff，包括了1MB(0~0xfffff），
    ; 所以用第 0 项来保证 loader 在分页机制下依然运行正确。
    ; 2
    ; 第768项以上的目录项用于内核空间, 也就是页表的 0xc0000000~0xffffffff 即 3072M~4G-1 共计 1G 属于内核
    ; 0x0 ~ 0xbfffffff 共计 3G 属于用户进程。
    ; 这样虚拟地址 0xc0000000~0xc03fffff之间的内存都指向的是低端 4MB 之内的物理地址，
    ; 这自然包括操作系统所占的低端 1MB 物理内存。
    or eax, PG_US_U | PG_RW_W | PG_P     ; 页目录项的属性 RW 和 P 位为 1, US 为 1 ，表示用户属性，所有特权级别都可以访问
    mov [PAGE_DIR_TABLE_POS + 0x0], eax  ; 第0个目录项写入第一个页表的位置及属性（0x101007?）
    mov [PAGE_DIR_TABLE_POS + 0xc00], eax  ; 0xc00=3072=768x4, 第768个目录项写入第一个页表的位置及属性
    
    sub eax, 0x1000                       ; 页目录项的地址+属性 0x100007
    mov [PAGE_DIR_TABLE_POS + 4092], eax  ; 使最后一个目录项指向页目录项根地址

; 下面创建页表项(PTE), 是第0个页目录项对应的页表，对应物理地址 0~0x3fffff
    mov ecx, 256  ; 1M 低端内存/每页大小4k = 256个页表项
    mov esi, 0
    mov edx, PG_US_U | PG_RW_W | PG_P  ; 属性为 7, OS=1, RW=1, P=1
.create_pte:  ; 创建 Page Table Entry
    mov [ebx+esi*4], edx  ; ebx为0x101000，即第一个页表的地址, 页表第0项映射到物理内存0x00000007，7是属性
    add edx, 4096         ; 每次加0x1000，即4k
    inc esi
    loop .create_pte      ; 在低端 1MB 内存中，虚拟地址等于物理地址。

; 创建内核其他页表的 PDE, 即第 769-1022 目录项，1023项指向了自己
    mov eax, PAGE_DIR_TABLE_POS
    add eax, 0x2000       ; 此时 eax 为第二个页表的位置
    or eax, PG_US_U | PG_RW_W | PG_P  ; 页目录项的属性 US 、RW 和 P 位都为 1
    mov ebx, PAGE_DIR_TABLE_POS
    mov ecx, 254          ; 范围为第 769-1022 的所有目录项数量
    mov esi, 769
.create_kernel_pde:
    mov [ebx+esi*4], eax  ; 在目录项中769-1022填入对应的2-255页表的地址（页表从1计数的）
    inc esi
    add eax, 0x1000
    loop .create_kernel_pde
    ret


;2 函数：将 kernel.bin 中的 segment 拷贝到编译的地址
kernel_init:
    xor eax, eax
    xor ebx, ebx  ; ebx 记录程序头表地址
    xor ecx, ecx  ; cx 记录程序头表中的 program header 数量
    xor edx, edx  ; dx 记录 program header 尺寸，即 e_phentsize

    mov dx, [KERNEL_BIN_BASE_ADDR + 42]  ; 偏移文件 42 字节处的属性是e_phentsize，表示program header大小
    mov ebx, [KERNEL_BIN_BASE_ADDR + 28] ; 偏移文件开始部分 28 字节的地方是 e_phoff, 表示第 1 个 program header 在文件中的偏移量
    ; 其实该值是 0x34 ，不过还是谨慎一点，这里来读取实际值
    
    add ebx, KERNEL_BIN_BASE_ADDR       ; 第1个程序头的位置
    mov cx, [KERNEL_BIN_BASE_ADDR + 44] ; 偏移文件开始部分44字节的地方是e_phnum，表示有几个 program header
.each_segment:
    cmp byte [ebx + 0], PT_NULL  ; 若 p_type等于PT_NULL ，说明此 program header 未使用
    je .PTNULL
    ; 为函数memcpy压入参数，参数是从右往左依次压入, 函数原型类似于 memcpy(dst, src, size)
    ; program header 中偏移 16 字节的地方是 p_filesz, 压入函数 memcpy 的第三个参数： size
    push dword [ebx + 16]
    ; 距程序头偏移量为 4 字节的位置是 p_offset
    ; 加上 kernel.bin 被加载到的物理地址， eax 为该段的物理地址
    mov eax, [ebx + 4]
    add eax, KERNEL_BIN_BASE_ADDR
    push eax  ; 压入函数 memcpy 的第二个参数:源地址
    push dword [ebx + 8]  ; 偏移程序头 8 字节的位置是 p_vaddr，这就是目的地址
    call mem_cpy
    add esp, 12  ; 清理栈中压入的三个参数
.PTNULL:
    add ebx, edx  ; ebx 指向下一个 program header
    loop .each_segment
    ret

;3 函数：逐字节拷贝 mem_cpy(dst, src, size) 
mem_cpy:
    cld
    push ebp
    mov ebp, esp
    push ecx  ; rep 指令用到了ecx, 但 ecx 对于外层段的循环还有用，故先入栈备份
    mov edi, [ebp + 8]   ; dst
    mov esi, [ebp + 12]  ; src
    mov ecx, [ebp + 16]  ; size
    rep movsb            ; 逐字节拷贝
    ; 恢复环境
    pop ecx
    pop ebp
    ret


;4 函数：保护模式的硬盘读取函数
rd_disk_m_32:

    mov esi, eax
    mov di, cx

    mov dx, 0x1f2
    mov al, cl
    out dx, al

    mov eax, esi

    mov dx, 0x1f3
    out dx, al

    mov cl, 8
    shr eax, cl
    mov dx, 0x1f4
    out dx, al

    shr eax, cl
    mov dx, 0x1f5
    out dx, al

    shr eax, cl
    and al, 0x0f
    or al, 0xe0
    mov dx, 0x1f6
    out dx, al

    mov dx, 0x1f7
    mov al, 0x20
    out dx, al

.not_ready:
    nop
    in al, dx
    and al, 0x88
    cmp al, 0x08
    jnz .not_ready

    mov ax, di
    mov dx, 256
    mul dx
    mov cx, ax
    mov dx, 0x1f0

.go_on_read:
    in ax, dx
    mov [ebx], ax
    add ebx, 2
    loop .go_on_read
    ret