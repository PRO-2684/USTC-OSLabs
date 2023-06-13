#ifndef FAT16_H
#define FAT16_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define FUSE_USE_VERSION 31
#include <fuse.h>

/* Unit size */
#define PHYSICAL_SECTOR_SIZE 512        // 每个物理扇区的大小
#define MAX_LOGICAL_SECTOR_SIZE 4096    // 逻辑扇区的最大大小
#define SEC_PER_TRACK  512            // 每个物理磁道的物理扇区数 （仅用于模拟磁盘）
#define DIR_ENTRY_SIZE 32              // 每个目录项的大小

// 文件属性，参考 https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system#DIR_OFS_0Bh
#define ATTR_NONE           0x00
#define ATTR_READONLY       0x01        // 只读
#define ATTR_HIDDEN         0x02        // 隐藏
#define ATTR_SYSTEM         0x04        // 系统
#define ATTR_VOLUME         0x08        // 卷标
#define ATTR_DIRECTORY      0x10        // 目录
#define ATTR_ARCHIVE        0x20        // 归档
#define ATTR_LFN            0x0F        // 长文件名项

#define ATTR_REGULAR        0x20        // 普通文件的属性，仅创建文件时使用

#define NAME_DELETED        0xE5        // 被删除的项
#define NAME_FREE           0x00        // 未使用的项

// 用户权限
#define S_IRUGO   (S_IRUSR | S_IRGRP | S_IROTH)
#define S_IXUGO   (S_IXUSR | S_IXGRP | S_IXOTH)
#define S_RDONLY  S_IRUGO                           // 只读文件的权限: 444
#define S_NORMAL  (S_IRUGO | S_IWUSR | S_IXUGO)     // 普通文件的权限: 755

#define FAT_NAME_LEN 11
#define FAT_NAME_BASE_LEN 8
#define FAT_NAME_EXT_LEN 3

#define MAX_NAME_LEN 512

typedef uint16_t cluster_t;
typedef uint64_t sector_t;
typedef uint8_t attr_t;


typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;

// 簇号（FAT 表项）
#define CLUSTER_FREE        0x0000u         // 未分配的簇号
#define CLUSTER_MIN         0x0002u         // 第一个代表簇的簇号
#define CLUSTER_MAX         0xFFEFu         // 最后一个代表簇的簇号
#define CLUSTER_END         0xFFFFu         // 文件结束的簇号
#define CLUSTER_END_BOUND   0xFFF8u         // 文件结束簇号界限，大于等于该数的簇号都被视为文件结束

#define DEFAULT_IMAGE       "fat16.img"

#define min(x, y) (((x) < (y)) ? (x) : (y))
#define max(x, y) (((x) > (y)) ? (x) : (y))

/* FAT16 BPB Structure */
typedef struct {
    BYTE BS_jmpBoot[3];
    BYTE BS_OEMName[8];
    WORD BPB_BytsPerSec; // 每个扇区的字节数。基本输入输出系统参数块从这里开始。
    BYTE BPB_SecPerClus; // 每簇扇区数
    WORD BPB_RsvdSecCnt; // 保留扇区数（包括主引导区）
    BYTE BPB_NumFATS; // 文件分配表数目，FAT16 文件系统中为 0x02, FAT2 作为 FAT1 的冗余
    WORD BPB_RootEntCnt; // 最大根目录条目个数
    WORD BPB_TotSec16;
    BYTE BPB_Media;
    WORD BPB_FATSz16; // 每个文件分配表的扇区数（FAT16 专用）
    WORD BPB_SecPerTrk;
    WORD BPB_NumHeads;
    DWORD BPB_HiddSec;
    DWORD BPB_TotSec32;
    BYTE BS_DrvNum;
    BYTE BS_Reserved1;
    BYTE BS_BootSig;
    DWORD BS_VollID;
    BYTE BS_VollLab[11];
    BYTE BS_FilSysType[8];
    BYTE Reserved2[448];
    WORD Signature_word;
} __attribute__ ((packed, aligned(__alignof__(DWORD)))) BPB_BS;
static_assert(sizeof(BPB_BS) == PHYSICAL_SECTOR_SIZE, "Wrong BPB size");

/* FAT Directory Structure */
typedef struct {
    BYTE DIR_Name[11]; // 文件名。前 8 个字节为文件名，后 3 个为拓展名。
    BYTE DIR_Attr; // 文件属性，取值为 0x10 表示为目录，0x20 表示为文件
    BYTE DIR_NTRes;
    BYTE DIR_CrtTimeTenth;
    WORD DIR_CrtTime;
    WORD DIR_CrtDate;
    WORD DIR_LstAccDate;
    WORD DIR_FstClusHI;
    WORD DIR_WrtTime;
    WORD DIR_WrtDate;
    WORD DIR_FstClusLO; // 文件首簇号 (FAT32 用作第一个簇的两个低字节)
    DWORD DIR_FileSize; // 文件大小
} __attribute__ ((packed, aligned(__alignof__(DWORD)))) DIR_ENTRY;
static_assert(sizeof(DIR_ENTRY) == DIR_ENTRY_SIZE, "Wrong DIR_ENTRY size");

enum FindResult {
    FIND_EXIST = 0,
    FIND_EMPTY = 1,
    FIND_FULL  = 2
};

int sector_read(sector_t sec_num, void *buffer);
int sector_write(sector_t sec_num, const void *buffer);

#endif