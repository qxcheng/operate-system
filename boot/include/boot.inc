;---------------------------loader和kernel-------------
LOADER_BASE_ADDR equ 0x900
LOADER_STACK_TOP equ LOADER_BASE_ADDR  ; 栈顶 0x900
LOADER_START_SECTOR equ 0x2

; gdt描述符属性
DESC_G_4K equ 100000000000000000000000b  ; 段描述符高23位，表示段界限的粒度为4KB 
DESC_D_32 equ 10000000000000000000000b   ; D/B位，1表示运行在32位模式下
DESC_L    equ 0000000000000000000000b    ; 高21位，如果为1表示为64位代码段，目前我们都是在32位模式下操作，故为零
DESC_AVL  equ 000000000000000000000b     ; 没有明确的用途，取值随意
DESC_LIMIT_CODE2 equ 11110000000000000000b  ; 第二部分段界限值，由于采用了32位平坦模型，所以段界限为(4GB/4KB)-1=0xFFFFF，故为全1
DESC_LIMIT_DATA2 equ DESC_LIMIT_CODE2
DESC_LIMIT_VIDEO2 equ 00000000000000000000000000001011b  ; 书中取全零，怀疑是错误的，因为保护模式的基地址是0xb8000，所以最后8位应该是b，而不是0
DESC_P equ 1000000000000000b
DESC_DPL_0 equ 000000000000000b
DESC_DPL_1 equ 010000000000000b
DESC_DPL_2 equ 100000000000000b
DESC_DPL_3 equ 110000000000000b
DESC_S_CODE equ  1000000000000b
DESC_S_DATA equ  DESC_S_CODE
DESC_S_sys equ  0000000000000b
DESC_TYPE_CODE equ 100000000000b ;x=1,c=0,r=0,a=0 代码段是可执行的，非一致性，不可读，已访问位 a 清0
DESC_TYPE_DATA equ 001000000000b ;x=0,e=0,w=1,a=0 数据段是不可执行的，向上扩展的，可写，已访问位 a 清0

; 代码段描述符的高32位（4字节）表示，其中(0x00 << 24表示最高8位的段基址值，由于我们采用的是平坦模型，故基址为零)，后面唯一可变的就是段界限值
DESC_CODE_HIGH4 equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + \
    DESC_L + DESC_AVL + DESC_LIMIT_CODE2 + \
    DESC_P + DESC_DPL_0 + DESC_S_CODE + DESC_TYPE_CODE + 0x00

DESC_DATA_HIGH4 equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + \
    DESC_L + DESC_AVL + DESC_LIMIT_DATA2 + \
    DESC_P + DESC_DPL_0 + DESC_S_DATA + DESC_TYPE_DATA + 0x00

DESC_VIDEO_HIGH4 equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + \
    DESC_L + DESC_AVL + DESC_LIMIT_VIDEO2 + \
    DESC_P + DESC_DPL_0 + DESC_S_DATA + DESC_TYPE_DATA + 0x00

; 选择子属性，最终的选择子在 loader.S 中定义
RPL0 equ 00b
RPL1 equ 01b
RPL2 equ 10b
RPL3 equ 11b
TI_GDT equ 000b
TI_LDT equ 100b

; 页目录表位置
PAGE_DIR_TABLE_POS equ 0x100000
; 页表相关属性
PG_P equ 1b
PG_RW_R equ 00b
PG_RW_W equ 10b
PG_US_S equ 000b
PG_US_U equ 100b

; kernel
KERNEL_START_SECTOR equ 0x9
KERNEL_BIN_BASE_ADDR equ 0x70000
PT_NULL equ 0
KERNEL_ENTRY_POINT equ 0xc0001500
