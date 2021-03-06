#ifndef __FS_INODE_H
#define __FS_INODE_H

#include "stdint.h"
#include "kernel/list.h"

// inode
struct inode {
    uint32_t i_no;                // inode 编号
    /* 当此inode是文件，i_size 指文件大小， 
       若此inode是目录，i_size 指该目录下所有目录项大小之和 */
    uint32_t i_size;              
    uint32_t i_open_cnts;         // 记录此文件被打开的次数
    bool write_deny;              // 写文件不能并行，进程写文件前检查此标识

    /* i_sectors[0-11］是直接块， i_sectors[12]用来存储一级间接块指针 */
    uint32_t i_sectors[13];
    struct list_elem inode_tag;
};

#endif