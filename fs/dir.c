#include "dir.h"
#include "stdint.h"
#include "inode.h"
#include "file.h"
#include "fs.h"
#include "kernel/stdio-kernel.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "string.h"
#include "interrupt.h"
#include "super_block.h"

struct dir root_dir;             // 根目录


/* 打开根目录 */
void open_root_dir(struct partition* part) {
    root_dir.inode = inode_open(part, part->sb->root_inode_no);
    root_dir.dir_pos = 0;
}

/* 在分区part上打开i结点为inode_no的目录并返回目录指针 */
struct dir* dir_open(struct partition* part, uint32_t inode_no) {
    struct dir* pdir = (struct dir*)sys_malloc(sizeof(struct dir));
    pdir->inode = inode_open(part, inode_no);
    pdir->dir_pos = 0;
    return pdir;
}

/* 在part分区内的pdir目录内寻找名为name的文件或目录，
   找到后返回true并将其目录项存入dir_e ，否则返回false  */
bool search_dir_entry(struct partition* part, struct dir* pdir, const char* name, struct dir_entry* dir_e) {
    uint32_t block_cnt = 140;  // 12个直接块＋128 个一级间接块＝140块                   
    uint32_t* all_blocks = (uint32_t*)sys_malloc(48 + 512);
    if (all_blocks == NULL) {
        printk("search_dir_entry: sys_malloc for all_blocks failed!");
        return false;
    }

    uint32_t block_idx = 0;
    while (block_idx < 12) {
        all_blocks[block_idx] = pdir->inode->i_sectors[block_idx];
        block_idx++;
    }
    block_idx = 0;

    // 若含有一级间接块表
    if (pdir->inode->i_sectors[12] != 0) {
        ide_read(part->my_disk, pdir->inode->i_sectors[12], all_blocks + 12, 1);
    }
    // 至此，all_blocks 存储的是该文件或目录的所有扇区地址

    /* 写目录项的时候己保证目录项不跨扇区，
       这样读目录项时容易处理，只申请容纳 1 个扇区的内存 */
    uint8_t* buf = (uint8_t*)sys_malloc(SECTOR_SIZE);
    struct dir_entry* p_de = (struct dir_entry*)buf;
    uint32_t dir_entry_size = part->sb->dir_entry_size;  // 目录项结构大小
    uint32_t dir_entry_cnt = SECTOR_SIZE / dir_entry_size;  // 1 扇区内可容纳的目录项个数

    // 开始在所有块中查找目录项(因为前面的可能被删除)
    while (block_idx < block_cnt) {
        if (all_blocks[block_idx] == 0) {
            block_idx++;
            continue;
        }
        ide_read(part->my_disk, all_blocks[block_idx], buf, 1);

        uint32_t dir_entry_idx = 0;
        // 遍历扇区中所有目录项
        while (dir_entry_idx < dir_entry_cnt) {
            if (!strcmp(p_de->filename, name)) {
                memcpy(dir_e, p_de, dir_entry_size);
                sys_free(buf);
                sys_free(all_blocks);
                return true;
            }
            dir_entry_idx++;
            p_de++;
        }
        block_idx++;
        // 此时p_de指向扇区内最后一个完整目录项，需要恢复p_de指向为buf
        p_de = (struct dir_entry*)buf;  
        memset(buf, 0, SECTOR_SIZE);    // 将buf清0，下次再用
    }
    sys_free(buf);
    sys_free(all_blocks);
    return false;
}

/* 关闭目录 */
void dir_close(struct dir* dir) {
    /* 根目录自打开后就不应该关闭，否则还需要再次open_root_dir();
       root_dir 所在的内存是低端 1MB 之内，并非在堆中，free会出问题 */
    if (dir == &root_dir) {
        return;
    }
    inode_close(dir->inode);
    sys_free(dir);
}

/* 在内存中初始化目录项 p_de */
void create_dir_entry(char* filename, uint32_t inode_no, uint8_t file_type, struct dir_entry* p_de) {
    ASSERT(strlen(filename) <= MAX_FILE_NAME_LEN);
    memcpy(p_de->filename, filename, strlen(filename));
    p_de->i_no = inode_no;
    p_de->f_type = file_type;
}

/* 将目录项 p_de 写入父目录 parent_dir 中 ， io_buf 由主调函数提供 */
bool sync_dir_entry(struct dir* parent_dir, struct dir_entry* p_de, void* io_buf) {
    struct inode* dir_inode = parent_dir->inode;
    uint32_t dir_size = dir_inode->i_size;  // 目录项总大小
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;  // 单个目录项大小
    ASSERT(dir_size % dir_entry_size == 0);
    uint32_t dir_entrys_per_sec = (512 / dir_entry_size);  // 每扇区最大的目录项数目

    int32_t block_lba = -1;
    /* 将该自录的所有扇区地址(12个直接块＋128个间接块）存入all_blocks */
    // 这里应该有BUG
    uint8_t block_idx = 0;
    uint32_t all_blocks[140] = {0};
    while (block_idx < 12) {
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }
    // dir_e 用来在 io_buf 中遍历目录项
    struct dir_entry* dir_e = (struct dir_entry*)io_buf;

    int32_t block_bitmap_idx = -1;

    /* 开始遍历所有块以寻找目录项空位，若已有扇区中没有空闲位，
       在不超过文件大小的情况下申请新扇区来存储新目录项 */
    block_idx = 0;
    while (block_idx < 140) {
        block_bitmap_idx = -1;
        // 在三种情况下分配块
        if (all_blocks[block_idx] == 0) {
            block_lba = block_bitmap_alloc(cur_part);
            if (block_lba == -1) {
                printk("alloc block bitmap for sync_dir_entry failed\n");
                return false;
            }
            // 分配块后同步 block_bitmap 
            block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
            ASSERT(block_bitmap_idx != -1);
            bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

            // 将块地址写入父目录的inode中
            block_bitmap_idx = -1;
            if (block_idx < 12) {          // 若是直接块
                dir_inode->i_sectors[block_idx] = all_blocks[block_idx] =block_lba;
            } else if (block_idx == 12) {  // 若是尚未分配一级间接块表
                // 将上面分配的块作为一级间接块表地址
                dir_inode->i_sectors[12] = block_lba;
                block_lba = -1;
                // 再分配一个块作为第0个间接块
                block_lba = block_bitmap_alloc(cur_part);
                // 发生错误归还上一个申请的块
                if (block_lba == -1) {
                    block_bitmap_idx = dir_inode->i_sectors[12] - cur_part->sb->data_start_lba;
                    bitmap_set(&cur_part->block_bitmap, block_bitmap_idx, 0);
                    dir_inode->i_sectors[12] = 0;
                    printk("alloc block bitmap for sync_dir_entry failed\n");
                    return false;
                }
                // 分配块后同步 block_bitmap
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                ASSERT(block_bitmap_idx != -1);
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

                all_blocks[12] = block_lba;  // 第一个间接块
                // 把新分配的第0个间接块地址写入一级间接块表
                ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1); 
            } else {  // 若是间接块未分配
                all_blocks[block_idx] = block_lba;
                // 把新分配的第（block_idx-12）个间接块地址写入一级间接块表
                ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
            }
            // 再将新目录项 p_de 写入新分配的间接块
            memset(io_buf, 0, 512);
            memcpy(io_buf, p_de, dir_entry_size);
            ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
            dir_inode->i_size += dir_entry_size;
            return true;
        }
        /* 第 block_idx 块已存在，将其读进内存, 然后在该块中查找空目录项 */
        ide_read(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
        // 在扇区内查找空目录项
        uint8_t dir_entry_idx = 0;
        while (dir_entry_idx < dir_entrys_per_sec) {
            if ((dir_e + dir_entry_idx)->f_type == FT_UNKNOWN) {
                /*  FT_ONKNOWN 为 0 ，无论是初始化，或是删除文件后，
                    都会将 f_type 置为 FT_UNKNOWN */ 
                memcpy(dir_e + dir_entry_idx, p_de, dir_entry_size);
                ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
                dir_inode->i_size += dir_entry_size;
                return true;
            }
            dir_entry_idx++;
        }
        block_idx++;
    }
    printk("directory is full!\n");
    return false;
}

/* 把分区 part 目录 pdir 中编号为 inode_no 的目录项删除 */
bool delete_dir_entry(struct partition* part, struct dir* pdir, uint32_t inode_no, void* io_buf) {
    struct inode* dir_inode = pdir->inode;
    uint32_t block_idx = 0, all_blocks[140] = {0};
    while (block_idx < 12) {
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }    
    if (dir_inode->i_sectors[12]) {
        ide_read(part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
    }

    // 目录项在存储时保证不会跨扇区
    uint32_t dir_entry_size = part->sb->dir_entry_size;
    // 每扇区最大的目录项数目
    uint32_t dir_entry_per_sec = (SECTOR_SIZE / dir_entry_size);
    struct dir_entry* dir_e = (struct dir_entry*)io_buf;
    struct dir_entry* dir_entry_found = NULL;
    uint8_t dir_entry_idx, dir_entry_cnt;
    bool is_dir_first_block = false;  // 目录的第 1 个块

    // 遍历所有块，寻找目录项
    block_idx = 0;
    while (block_idx < 140) {
        is_dir_first_block = false;
        if (all_blocks[block_idx] == 0) {
            block_idx++;
            continue;
        }
        dir_entry_idx = dir_entry_cnt = 0;
        memset(io_buf, 0, SECTOR_SIZE);
        // 读取扇区，获得目录项
        ide_read(part->my_disk, all_blocks[block_idx], io_buf, 1);

        // 遍历所有的目录项，统计该扇区的目录项数量及是否有待删除的目录项
        while (dir_entry_idx < dir_entry_per_sec) {
            if ((dir_e + dir_entry_idx)->f_type != FT_UNKNOWN) {
                if (!strcmp((dir_e + dir_entry_idx)->filename, ".")) {
                    is_dir_first_block = true;
                } else if (strcmp((dir_e + dir_entry_idx)->filename, ".") && strcmp((dir_e + dir_entry_idx)->filename, "..")) {
                    dir_entry_cnt++;  // 统计此扇区内的目录项个数，用来判断删除目录项后是否回收该扇区
                    // 如果找到此 i 结点，就将其记录在 dir_entry_found
                    if ((dir_e + dir_entry_idx)->i_no == inode_no) {
                        ASSERT(dir_entry_found == NULL);
                        dir_entry_found = dir_e + dir_entry_idx; 
                        // 找到后也继续遍历，统计总共的目录项数
                    }
                }
            }
            dir_entry_idx++;
        }
        // 若此扇区未找到该目录项，继续在下个扇区中找
        if (dir_entry_found == NULL) {
            block_idx++;
            continue;
        }
        /* 在此扇区中找到目录项后，清除该目录项并判断是否回收扇区，随后直接退出循环 */
        ASSERT(dir_entry_cnt >= 1);
        // 除目录第 1 个扇区外，若该扇区上只有该目录项自己，则将整个扇区回收
        if (dir_entry_cnt == 1 && !is_dir_first_block) {
            /* a 在块位图中回收该块 */
            uint32_t block_bitmap_idx = all_blocks[block_idx] - part->sb->data_start_lba;
            bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
            bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

            /* b 将块地址从数组 i_sectors 或索引表中去掉 */
            if (block_idx < 12) {
                dir_inode->i_sectors[block_idx] = 0;
            // 在一级间接索引表中擦除该间接块地址
            } else {
                // 先判断一级间接索引表中间接块的数量，
                // 如果仅有这 1 个间接块，连同间接索引表所在的块一同回收
                uint32_t indirect_blocks = 0;
                uint32_t indirect_block_idx = 12;
                while (indirect_block_idx < 140) {
                    if (all_blocks[indirect_block_idx] != 0) {
                        indirect_blocks++;
                    }
                }
                ASSERT(indirect_blocks >= 1);  // 包括当前间接块
                // 间接索引表中还包括其他间接块，仅在索引表中擦除当前这个间接块地址
                if (indirect_blocks > 1) {
                    all_blocks[block_idx] = 0;
                    ide_write(part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
                // 间接索引表中就当前这1个间接块, 直接把间接索引表所在的块回收，然后擦除间接索引表块地址
                } else {
                    block_bitmap_idx = dir_inode->i_sectors[12] - part->sb->data_start_lba;
                    bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
                    bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
                    dir_inode->i_sectors[12] = 0;  // 将间接索引表地址清 0
                }
            }
        // 仅将该目录项清空
        } else {
            memset(dir_entry_found, 0, dir_entry_size);
            ide_write(part->my_disk, all_blocks[block_idx], io_buf, 1);
        }
        // 更新 i 结点信息并同步到硬盘
        ASSERT(dir_inode->i_size >= dir_entry_size);
        dir_inode->i_size -= dir_entry_size;
        memset(io_buf, 0, SECTOR_SIZE * 2);
        inode_sync(part, dir_inode, io_buf);

        return true;
    }
    // 所有块中未找到则返回 false ，出现这种情况应该是 serarch_file 出错了
    return false;
}

/* 读取目录，成功返回 1 个目录项，失败返回 NULL */
struct dir_entry* dir_read(struct dir* dir) {
    struct dir_entry* dir_e = (struct dir_entry*)dir->dir_buf;
    struct inode* dir_inode = dir->inode;
    uint32_t all_blocks[140] = {0}, block_cnt = 12;
    uint32_t block_idx = 0, dir_entry_idx = 0;

    while (block_idx < 12) {
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }
    if (dir_inode->i_sectors[12] != 0) {
        ide_read(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
        block_cnt = 140;
    }
    block_idx = 0;

    uint32_t cur_dir_entry_pos = 0;  // 当前目录项的偏移，此项用来判断是否是之前已经返回过的目录项
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
    uint32_t dir_entrys_per_sec = SECTOR_SIZE / dir_entry_size;

    // 在目录大小内遍历
    while (dir->dir_pos < dir_inode->i_size) {
        if (dir->dir_pos >= dir_inode->i_size) {
            return NULL;
        }
        if (all_blocks[block_idx] == 0) {
            block_idx++;
            continue;
        }
        memset(dir_e, 0, SECTOR_SIZE);
        ide_read(cur_part->my_disk, all_blocks[block_idx], dir_e, 1);
        dir_entry_idx = 0;
        // 遍历扇区内所有目录项
        while (dir_entry_idx < dir_entrys_per_sec) {
            // 如果 f_type 不等于 0 ，即不等于 FT UNKNOWN
            if ((dir_e + dir_entry_idx)->f_type) {
                // 判断是不是最新的目录项，避免返回曾经已经返回过的目录项
                if (cur_dir_entry_pos < dir->dir_pos) {
                    cur_dir_entry_pos += dir_entry_size;
                    dir_entry_idx++;
                    continue;
                }
                ASSERT(cur_dir_entry_pos == dir->dir_pos);
                dir->dir_pos += dir_entry_size;  // 更新为新位置，即下一个返回的目录项地址
                return dir_e + dir_entry_idx;
            }
            dir_entry_idx++;
        }
        block_idx++;
    }
    return NULL;
}

/* 判断目录是否为空 */
bool dir_is_empty(struct dir* dir) {
    struct inode* dir_inode = dir->inode;
    // 若目录下只有．和.. 这两个目录项，则目录为空
    return (dir_inode->i_size == cur_part->sb->dir_entry_size * 2);
}

/* 在父目录 parent_dir 中删除 child_dir */
int32_t dir_remove(struct dir* parent_dir, struct dir* child_dir) {
    struct inode* child_dir_inode = child_dir->inode;
    // 空目录只在 inode->i_sectors[0] 中有扇区，其他扇区都应该为空
    int32_t block_idx = 1;
    while (block_idx < 13) {
        ASSERT(child_dir_inode->i_sectors[block_idx] == 0);
        block_idx++;
    }
    void* io_buf = sys_malloc(SECTOR_SIZE * 2);
    if (io_buf == NULL) {
        printk("dir_remove: malloc for io_buf failed\n");
        return -1;
    }
    // 在父目录 parent_dir 中删除子目录 child dir 对应的目录项
    delete_dir_entry(cur_part, parent_dir, child_dir_inode->i_no, io_buf);
    // 回收 inode 中 i secotrs 中所占用的扇区，并同步 inode_bitmap 和 block_bitmap
    inode_release(cur_part, child_dir_inode->i_no);
    sys_free(io_buf);
    return 0;
}