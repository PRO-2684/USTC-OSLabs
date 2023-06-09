#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "fat16.h"

/* FAT16 volume data with a file handler of the FAT16 image file */
// 存储 FAT 文件系统所需要的元数据的数据结构
typedef struct {
    uint32_t sector_size;   // 逻辑扇区大小（字节）
    uint32_t sec_per_clus;  // 每簇扇区数
    uint32_t reserved;      // 保留扇区数
    uint32_t fats;          // FAT 表的数量
    uint32_t dir_entries;   // 根目录项数量
    uint32_t sectors;       // 文件系统总扇区数
    uint32_t sec_per_fat;   // 每个 FAT 表所占扇区数

    sector_t fat_sec;       // FAT 表开始扇区
    sector_t root_sec;      // 根目录区域开始扇区
    uint32_t root_sectors;  // 根目录区域扇区数
    sector_t data_sec;      // 数据区域开始扇区

    uint32_t clusters;      // 文件系统簇数
    uint32_t cluster_size;  // 簇大小（字节）

    uid_t fs_uid;           // 可忽略，挂载 FAT 的用户 ID，所有文件的拥有者都显示为该用户
    gid_t fs_gid;           // 可忽略，挂载 FAT 的组 ID，所有文件的用户组都显示为该组
    struct timespec atime;  // 可忽略，访问时间
    struct timespec mtime;  // 可忽略，修改时间
    struct timespec ctime;  // 可忽略，创建时间
} FAT16;

FAT16 meta;

#define ATTR_CONTAINS(attr, attr_name) ((attr & attr_name) != 0)

size_t sector_offset(sector_t sector) {
    return sector * meta.sector_size;
}

/**
 * @brief 簇号是否是合法的（表示正在使用的）数据簇号（在 CLUSTER_MIN 和 CLUSTER_MAX 之间）
 *
 * @param clus 簇号
 * @return bool
 */
bool is_cluster_inuse(cluster_t clus) {
    return CLUSTER_MIN <= clus && clus <= CLUSTER_MAX;
}

sector_t cluster_first_sector(cluster_t clus) {
    assert(is_cluster_inuse(clus));
    return ((clus - 2) * meta.sec_per_clus) + meta.data_sec;
}

cluster_t sector_cluster(sector_t sec) {
    if (sec < meta.data_sec) {
        return 0;
    }
    cluster_t clus = 2 + (sec - meta.data_sec) / meta.sec_per_clus;
    assert(is_cluster_inuse(clus));
    return clus;
}

int is_cluster_end(cluster_t clus) {
    return clus >= CLUSTER_END_BOUND;
}

bool is_readonly(attr_t attr) {
    return (attr & ATTR_READONLY) != 0;
}

bool is_directory(attr_t attr) {
    return (attr & ATTR_DIRECTORY) != 0;
}

bool is_lfn(attr_t attr) {
    return attr == ATTR_LFN;
}

bool is_free(DIR_ENTRY* dir) {
    return dir->DIR_Name[0] == NAME_FREE;
}

bool is_deleted(DIR_ENTRY* dir) {
    return dir->DIR_Name[0] == NAME_DELETED;
}

bool is_valid(DIR_ENTRY* dir) {
    const uint8_t* name = dir->DIR_Name;
    attr_t attr = dir->DIR_Attr;
    return !is_lfn(attr) && name[0] != NAME_DELETED && name[0] != NAME_FREE;
}

bool is_dot(DIR_ENTRY* dir) {
    if (is_lfn(dir->DIR_Attr)) {
        return false;
    }
    const char* name = (const char*)dir->DIR_Name;
    attr_t attr = dir->DIR_Attr;
    const char DOT_NAME[] = ".";
    const char DOTDOT_NAME[] = "..";
    return strncmp(name, DOT_NAME, FAT_NAME_LEN) == 0 || strncmp(name, DOTDOT_NAME, FAT_NAME_LEN) == 0;
}

bool path_is_root(const char* path) {
    path += strspn(path, "/");
    return *path == '\0';
}

/**
 * @brief 将文件名转换为 FAT 的 8+3 文件名格式，存储在 res 中，res 是长度至少为 11 的 char 数组
 */
int to_shortname(const char* name, size_t len, char* res) {
    bool has_ext = false;
    size_t base_len = len;

    // 找到文件名基础部分和拓展名部分，注意最后一个点后是拓展名
    for (size_t i = 0; i < len; i++) {
        if (name[i] == '\0') {
            len = i;
            base_len = min(base_len, len);
            break;
        }
        const char INVALID_CHARS[] = "*?<>|\"+=,; :\\";
        if (strchr(INVALID_CHARS, name[i]) != NULL) {
            return -EINVAL;
        }
        if (name[i] == '.' && i != 0) {
            has_ext = true;
            base_len = i;
        }
    }

    // 转换文件名
    memset(res, ' ', FAT_NAME_LEN);
    for (size_t i = 0; i < base_len && i < FAT_NAME_BASE_LEN; i++) {
        res[i] = toupper(name[i]);
    }
    // 0xe5 用来代表删除，如果首个字母为 0xe5, 需要转换为 0x05
    res[0] = (res[0] == 0xe5) ? 0x05 : res[0];

    // 转换拓展名
    if (has_ext) {
        const char* ext = name + base_len + 1;
        size_t ext_len = len - base_len - 1;
        for (size_t i = 0; i < ext_len && i < FAT_NAME_EXT_LEN; i++) {
            res[FAT_NAME_BASE_LEN + i] = toupper(ext[i]);
        }
    }
    return 0;
}

/**
 * @brief 将 FAT 的 8+3 文件名格式转换为普通文件名，存储在 res 中，len 是 res 的长度
 */
int to_longname(const uint8_t fat_name[11], char* res, size_t len) {
    len--;  // last char for '\0'
    size_t i = 0;
    while (i < len && i < FAT_NAME_BASE_LEN) {
        if (fat_name[i] == ' ') {
            break;
        }
        res[i] = tolower(fat_name[i]);
        i++;
    }

    if (fat_name[FAT_NAME_BASE_LEN] != ' ') {
        res[i++] = '.';
        for (size_t j = FAT_NAME_BASE_LEN; i < len && j < FAT_NAME_LEN; j++) {
            if (fat_name[j] == ' ') {
                break;
            }
            res[i] = tolower(fat_name[j]);
            i++;
        }
    }

    res[i] = '\0';
    return i;
}

// 比较长文件名是否和 dir 目录项中的文件匹配
bool check_name(const char* name, size_t len, const DIR_ENTRY* dir) {
    char fatname[11];
    to_shortname(name, len, fatname);
    return strncmp(fatname, (const char*)dir->DIR_Name, FAT_NAME_LEN) == 0;
}

/**
 * @brief 读取簇号为 clus 对应的 FAT 表项
 *
 * @param clus 簇号
 * @return cluster_t FAT 返回值：对应 FAT 表项的值
 */
cluster_t read_fat_entry(cluster_t clus) {
    char sector_buffer[PHYSICAL_SECTOR_SIZE];
    // TODO: 1.4 读取簇号为 clus 对应的 FAT 表项，步骤如下：
    // 1. 计算簇号 clus 对应的 FAT 表项的偏移量，并计算表项所在的扇区号
    // 2. 使用 sector_read 函数读取该扇区
    // 3. 计算簇号 clus 对应的 FAT 表项在该扇区中的偏移量
    // 4. 从该偏移量处读取对应表项的值，并返回
    /** Your Code Here ... **/
}

/**
 * @brief 用于表示目录项查找结果的结构体
 */
typedef struct {
    DIR_ENTRY dir;    // 找到的目录项
    sector_t sector;  // 目录项所在的扇区
    size_t offset;    // 目录项在扇区中的偏移量
} DirEntrySlot;

/**
 * @brief 在 from_sector 开始的连续 sectors_count 个扇区中查找 name 对应的目录项
 *        找到对应目录项时返回 FIND_EXIST
 *        未找到对应目录项，但找到了空槽返回 FIND_EMPTY
 *        未找到对应目录项，且扇区都已满时返回 FIND_FULL
 *        出现其它错误，返回负数
 * @param name              要查找的文件名
 * @param len               文件名长度
 * @param from_sector       要查找的第一个扇区号
 * @param sectors_count     要查找的扇区数
 * @param slot              找到的目录项，参考对 DirEntrySlot 的注释
 * @return long
 */
int find_entry_in_sectors(const char* name, size_t len, sector_t from_sector, size_t sectors_count, DirEntrySlot* slot) {
    char buffer[PHYSICAL_SECTOR_SIZE];
    // 对每一个待查找的扇区：
    for (size_t i = 0; i < sectors_count; i++) {
        // TODO: 1.3 读取每一个扇区扇区，步骤如下：
        // 1. 使用 sector_read 函数读取从扇区号 from_sector 开始的第 i 个扇区
        // 2. 对该扇区中的每一个目录项，检查是否是待查找的目录项（注意检查目录项是否合法）
        for (size_t off = 0; off < meta.sector_size; off += DIR_ENTRY_SIZE) {
            DIR_ENTRY* dir = (DIR_ENTRY*)(buffer + off);
            // 3. 如果是待查找的目录项，将该目录项的信息填入 slot 中，并返回 FIND_EXIST
            // 4. 如果不是待查找的目录项，检查该目录项是否为空，如果为空，将该目录项的信息填入 slot 中，并返回 FIND_EMPTY
            // 5. 如果不是待查找的目录项，且该扇区中的所有目录项都不为空，返回 FIND_FULL
        }
    }
    return FIND_FULL;
}

/**
 * @brief 找到 path 所对应路径的目录项，如果最后一级路径不存在，则找到能创建最后一集文件 / 目录的空目录项。（这个函数同时实现了找目录项和找空槽的功能）
 *
 * @param path          需要查找的路径
 * @param slot          最后一级找到的目录项，参考对 DirEntrySlot 的注释
 * @param remains       path 中未找到的部分
 * @return int 成功返回 0，失败返回错误代码的负值，可能的错误参见 brief 部分。
 */
int find_entry_internal(const char* path, DirEntrySlot* slot, const char** remains) {
    *remains = path;
    *remains += strspn(*remains, "/");  // 跳过开头的'/'

    unsigned level = 0;
    cluster_t clus = CLUSTER_END;  // 当前查找到的目录项开始的簇号
    int state = FIND_EXIST;
    // 如果 remains 不为空，说明还有未找到的层级
    while (**remains != '\0' && state == FIND_EXIST) {
        size_t len = strcspn(*remains, "/");  // 目前要搜索的文件名长度
        // *remains 开始的，长为 len 的字符串是当前要搜索的文件名

        if (level == 0) {
            // 如果是第一级，需要从根目录开始搜索
            // TODO: 1.1 设置根目录的扇区号和扇区数（请给下面两个变量赋值，根目录的扇区号和扇区数可以在 meta 里的字段找到。）
            sector_t root_sec;
            size_t nsec;
            // 使用 find_entry_in_sectors 寻找相应的目录项
            state = find_entry_in_sectors(*remains, len, root_sec, nsec, slot);
            if (state != FIND_EXIST) {
                // 根目录项中没找到第一级路径，直接返回
                return state;
            }
        } else {
            // 不是第一级，在目录对应的簇中寻找（在上一级中已将 clus 设为第一个簇）
            while (is_cluster_inuse(clus)) {  // 依次查找每个簇
                // TODO: 1.2 在 clus 对应的簇中查找每个目录项。
                // 你可以使用 state = find_entry_in_sectors(.....)， 参数参考第一级中是如何查找的。

                if (state < 0) {  // 出现错误
                    return state;
                } else if (state == FIND_EXIST || state == FIND_EMPTY) {
                    break;  // 该级找到了，或者已经找完了有内容的项，不需要往后继续查找该级后面的簇
                }
                clus = read_fat_entry(clus);  // 记得实现该函数
            }

            // 下一级目录开始位置
            const char* next_level = *remains + len;
            next_level += strspn(next_level, "/");

            if (state == FIND_EXIST) {
                // 该级找到的情况，remains 后移至下一级
                level++;
                *remains = next_level;
                clus = slot->dir.DIR_FstClusLO;
            }

            if (*next_level != '\0') {
                // 不是最后一级，且没找到
                if (state != FIND_EXIST) {
                    return -ENOENT;
                }

                // 不是最后一级，且不是目录
                if (!is_directory(slot->dir.DIR_Attr)) {
                    return -ENOTDIR;
                }
            }
        }
    }
    return state;
}

/**
 * @brief 读目录、读文件时使用，找到 path 所对应路径的目录项。其实只是包装了一下 find_entry_internal
 *
 * @param path
 * @param slot
 * @return int
 */
int find_entry(const char* path, DirEntrySlot* slot) {
    const char* remains = NULL;
    int ret = find_entry_internal(path, slot, &remains);
    if (ret < 0) {
        return ret;
    }
    if (ret == FIND_EXIST) {
        return 0;
    }
    return -ENOENT;
}

/**
 * @brief 创建目录、创建文件时使用，找到一个空槽，并且顺便检查是否有重名文件 / 目录。
 *
 * @param path
 * @param slot
 * @param last_name
 * @return int
 */
int find_empty_slot(const char* path, DirEntrySlot* slot, const char** last_name) {
    int ret = find_entry_internal(path, slot, last_name);
    if (ret < 0) {
        return ret;
    }
    if (ret == FIND_EXIST) {  // 找到重名文件，返回文件已存在
        return -EEXIST;
    }
    if (ret == FIND_FULL) {  // 找不到空槽，返回目录已满
        return -ENOSPC;
    }
    return 0;
}

mode_t get_mode_from_attr(uint8_t attr) {
    mode_t mode = 0;
    mode |= is_readonly(attr) ? S_IRUGO : S_NORMAL;
    mode |= is_directory(attr) ? S_IFDIR : S_IFREG;
    return mode;
}

void time_fat_to_unix(struct timespec* ts, uint16_t date, uint16_t time, uint16_t acc_time) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = (date>> 9) + 80;        // 7 位年份
    t.tm_mon = ((date>> 5) & 0xf) - 1;  // 4 位月份
    t.tm_mday = date & 0x1f;             // 5 位日期

    t.tm_hour = time >> 11;         // 5 位小时
    t.tm_min = (time>> 5) & 0x3f;  // 6 位分钟
    t.tm_sec = (time & 0x1f) * 2;   // 5 位秒，需要乘 2，精确到 2 秒

    ts->tv_sec = mktime(&t) + (acc_time / 100);
    ts->tv_nsec = (acc_time % 100) * 10000000;
}

void time_unix_to_fat(const struct timespec* ts, uint16_t* date, uint16_t* time, uint8_t* acc_time) {
    struct tm* t = gmtime(&(ts->tv_sec));
    *date = 0;
    *date |= ((t->tm_year - 80) << 9);
    *date |= ((t->tm_mon + 1) << 5);
    *date |= t->tm_mday;

    if (time != NULL) {
        *time = 0;
        *time |= (t->tm_hour << 11);
        *time |= (t->tm_min << 5);
        *time |= (t->tm_sec / 2);
    }

    if (acc_time != NULL) {
        *acc_time = (t->tm_sec % 2) * 100;
        *acc_time += ts->tv_nsec / 10000000;
    }
}

// =========================== 文件系统接口实现 ===============================

/**
 * @brief 文件系统初始化，无需修改
 *
 * @param conn
 * @return void*
 */
void* fat16_init(struct fuse_conn_info* conn, struct fuse_config* config) {
    /* Reads the BPB */
    BPB_BS bpb;
    sector_read(0, &bpb);

    // TODO: 0.0 你无需修改这部分代码，但阅读这部分，并理解这些变量的含义有助于你理解文件系统的结构
    // 请同时参考 FAT16 结构体的定义里的注释（本文件第 15 行开始）
    meta.sector_size = bpb.BPB_BytsPerSec;
    meta.sec_per_clus = bpb.BPB_SecPerClus;
    meta.reserved = bpb.BPB_RsvdSecCnt;
    meta.fats = bpb.BPB_NumFATS;
    meta.dir_entries = bpb.BPB_RootEntCnt;
    meta.sectors = bpb.BPB_TotSec16 != 0 ? bpb.BPB_TotSec16 : bpb.BPB_TotSec32;
    meta.sec_per_fat = bpb.BPB_FATSz16;

    meta.fat_sec = meta.reserved;
    meta.root_sec = meta.fat_sec + (meta.fats * meta.sec_per_fat);
    meta.root_sectors = (meta.dir_entries * DIR_ENTRY_SIZE) / meta.sector_size;
    meta.data_sec = meta.root_sec + meta.root_sectors;
    meta.clusters = (meta.sectors - meta.data_sec) / meta.sec_per_clus;
    meta.cluster_size = meta.sec_per_clus * meta.sector_size;

    // 以下可忽略
    meta.fs_uid = getuid();
    meta.fs_gid = getgid();

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    meta.atime = meta.mtime = meta.ctime = now;
    return NULL;
}

/**
 * @brief 释放文件系统，无需修改
 *
 * @param data
 */
void fat16_destroy(void* data) {}

/**
 * @brief 获取 path 对应的文件的属性，无需修改
 *
 * @param path    要获取属性的文件路径
 * @param stbuf   输出参数，需要填充的属性结构体
 * @return int    成功返回 0，失败返回 POSIX 错误代码的负值
 */
int fat16_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi) {
    // 清空所有属性
    memset(stbuf, 0, sizeof(struct stat));

    // 这些属性被忽略
    stbuf->st_dev = 0;
    stbuf->st_ino = 0;
    stbuf->st_nlink = 0;
    stbuf->st_rdev = 0;

    // 这些属性被提前计算好，不会改变
    stbuf->st_uid = meta.fs_uid;
    stbuf->st_gid = meta.fs_gid;
    stbuf->st_blksize = meta.cluster_size;

    // 这些属性需要根据文件设置
    // st_mode, st_size, st_blocks, a/m/ctim
    if (path_is_root(path)) {
        stbuf->st_mode = S_IFDIR | S_NORMAL;
        stbuf->st_size = 0;
        stbuf->st_blocks = 0;
        stbuf->st_atim = meta.atime;
        stbuf->st_mtim = meta.mtime;
        stbuf->st_ctim = meta.ctime;
        return 0;
    }

    DirEntrySlot slot;
    DIR_ENTRY* dir = &(slot.dir);
    int ret = find_entry(path, &slot);
    if (ret < 0) {
        return ret;
    }
    stbuf->st_mode = get_mode_from_attr(dir->DIR_Attr);
    stbuf->st_size = dir->DIR_FileSize;
    stbuf->st_blocks = dir->DIR_FileSize / PHYSICAL_SECTOR_SIZE;

    time_fat_to_unix(&stbuf->st_atim, dir->DIR_LstAccDate, 0, 0);
    time_fat_to_unix(&stbuf->st_mtim, dir->DIR_WrtDate, dir->DIR_WrtTime, 0);
    time_fat_to_unix(&stbuf->st_ctim, dir->DIR_CrtDate, dir->DIR_CrtTime, dir->DIR_CrtTimeTenth);
    return 0;
}

// ------------------TASK1: 读目录、读文件 -----------------------------------

/**
 * @brief 读取 path 对应的目录，得到目录中有哪些文件，结果通过 filler 函数写入 buffer 中
 *        例如，如果 path 是 / a/b，而 / a/b 下有 apple、orange、banana 三个文件，那么我们的函数中应该调用 filler 三次：
 *          filler(buf, "apple", NULL, 0)
 *          filler(buf, "orange", NULL, 0)
 *          filler(buf, "banana", NULL, 0)
 *        然后返回 0，这样就告诉了 FUSE，/a/b 目录下有这三个文件。
 *
 * @param path    要读取目录的路径
 * @param buf     结果缓冲区
 * @param filler  用于填充结果的函数，本次实验按 filler(buffer, 文件名, NULL, 0) 的方式调用即可。
 *                你也可以参考 <fuse.h> 第 58 行附近的函数声明和注释来获得更多信息。
 * @param offset  忽略
 * @param fi      忽略
 * @return int    成功返回 0，失败返回 POSIX 错误代码的负值
 */
int fat16_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags) {
    // 这里解释本函数的思路：
    //   1. path 是我们要读取的路径，有两种情况，path 是根目录，path 不是根目录。
    //   2. 如果 path 是根目录，那么我们读取根目录区域的内容即可。
    //   3. 如果 path 不是根目录，那么我们需要找到 path 对应的目录项（这部分逻辑在 find_entry 和 find_entry_internal 两个函数中。）
    //   4. 目录项中保存了 path 对应的目录的第一个簇号，我们读取簇中每个目录项的内容即可。
    //   5. 使用 filler 函数将每个目录项的文件名写入 buf 中。

    bool root = path_is_root(path);
    DIR_ENTRY dir;
    cluster_t clus = CLUSTER_END;
    if (!root) {
        DirEntrySlot slot;
        DIR_ENTRY* dir = &(slot.dir);

        // Hint: find_entry 找到路径对应的目录项，是这个函数最难的部分。
        // find_entry 在后面所有函数中都会多次使用，请阅读、理解并补全它的实现。
        int ret = find_entry(path, &slot);
        if (ret < 0) {
            return ret;
        }
        clus = dir->DIR_FstClusLO;  // 不是根目录
        if (!is_directory(dir->DIR_Attr)) {
            return -ENOTDIR;
        }
    }

    // 要读的目录项的第一个簇位于 clus，请你读取该簇中的所有目录项。
    char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    char name[MAX_NAME_LEN];
    while (root || is_cluster_inuse(clus)) {
        sector_t first_sec;
        size_t nsec;
        if (root) {
            first_sec = meta.root_sec;
            nsec = meta.root_sectors;
        } else {
            first_sec = cluster_first_sector(clus);
            nsec = meta.sec_per_clus;
        }

        // TODO: 1.5 读取当前簇中每一个扇区内所有目录项，并将合法的项，使用 filler 函数的项的文件名写入 buf 中。
        // filler 的使用方法： filler(buf, 文件名, NULL, 0)
        // 你可以参考 find_entry_in_sectors 函数的实现。
        for (size_t i = 0; i < nsec; i++) {
            sector_t sec = first_sec + i;
            sector_read(sec, sector_buffer);
            // TODO: 1.5 对扇区中每个目录项：
            // 1. 确认其是否是表示文件或目录的项（排除 LFN、空项、删除项等不合法项的干扰）
            // 2. 从 FAT 文件名中，获得长文件名（可以使用提供的 to_longname 函数）
            // 3. 使用 filler 填入 buf
            // 4. 找到空项即可结束查找（说明后面均为空）。
        }

        if (root) {
            break;
        }

        clus = read_fat_entry(clus);
    }

    return 0;
}

/**
 * @brief 从簇 clus 的 offset 处开始读取 size 字节的数据到 data 中，并返回实际读取的字节数。
 *
 * @param clus      要读取的簇号
 * @param offset    要读取的数据在簇中的偏移量
 * @param data      结果缓冲区
 * @param size      要读取的数据长度
 * @return int
 */
int read_from_cluster_at_offset(cluster_t clus, off_t offset, char* data, size_t size) {
    // printf("Read clus %hd at offset %ld, size: %lu\n", clus, offset, size);
    assert(offset + size <= meta.cluster_size);  // offset + size 必须小于簇大小
    char sector_buffer[PHYSICAL_SECTOR_SIZE];

    uint32_t sec = cluster_first_sector(clus) + offset / meta.sector_size;
    size_t sec_off = offset % meta.sector_size;
    size_t pos = 0;
    while (pos < size) {
        int ret = sector_read(sec, sector_buffer);
        if (ret < 0) {
            return ret;
        }
        size_t len = min(meta.sector_size - sec_off, size - pos);
        memcpy(data + pos, sector_buffer + sec_off, len);
        pos += len;
        sec_off = 0;
        sec++;
    }
    return size;
}

/**
 * @brief 从 path 对应的文件的 offset 字节处开始读取 size 字节的数据到 buffer 中，并返回实际读取的字节数。
 * Hint: 文件大小属性是 Dir.DIR_FileSize。
 *
 * @param path    要读取文件的路径
 * @param buffer  结果缓冲区
 * @param size    需要读取的数据长度
 * @param offset  要读取的数据所在偏移量
 * @param fi      忽略
 * @return int    成功返回实际读写的字符数，失败返回 0。
 */
int fat16_read(const char* path, char* buffer, size_t size, off_t offset, struct fuse_file_info* fi) {
    printf("read(path='%s', offset=%ld, size=%lu)\n", path, offset, size);
    if (path_is_root(path)) {
        return -EISDIR;
    }

    DirEntrySlot slot;
    DIR_ENTRY* dir = &(slot.dir);
    int ret = find_entry(path, &slot);
    if (ret < 0) {
        return ret;
    }
    if (is_directory(dir->DIR_Attr)) {
        return -EISDIR;
    }
    if (offset> dir->DIR_FileSize) {
        return -EINVAL;
    }
    size = min(size, dir->DIR_FileSize - offset);

    cluster_t clus = dir->DIR_FstClusLO;
    size_t p = 0;
    // TODO: 1.6 clus 初始为该文件第一个簇，利用 read_from_cluster_at_offset 函数，从正确的簇中读取数据。
    // Hint: 需要注意 offset 的位置，和结束读取的位置。要读取的数据可能横跨多个簇，也可能就在一个簇的内部。
    // 你可以参考 read_from_cluster_at_offset 里，是怎么处理每个扇区的读取范围的，或者用自己的方式解决这个问题。

    return p;
}

// ------------------TASK2: 创建 / 删除文件 -----------------------------------

/**
 * @brief 将 DirEntry 写入 Slot 里对应的目录项。
 *        注意，我们只能用 sector_write() 写入整个扇区的数据，所以要修改单个目录项，需要先读出整个扇区
 *        然后修改目录项对应的部分，然后将整个扇区写回。
 * @param slot 要写入的目录项，及目录项所在的扇区号和偏移量
 * @return int
 */
int dir_entry_write(DirEntrySlot slot) {
    char sector_buffer[PHYSICAL_SECTOR_SIZE];
    // TODO: 2.1
    //  1. 读取 slot.dir 所在的扇区
    //  2. 将目录项写入 buffer 对应的位置（Hint: 使用 memcpy）
    //  3. 将整个扇区完整写回
    return 0;
}

int dir_entry_create(DirEntrySlot slot, const char* shortname, attr_t attr, cluster_t first_clus, size_t file_size) {
    DIR_ENTRY* dir = &(slot.dir);
    memset(dir, 0, sizeof(DIR_ENTRY));

    memcpy(dir, shortname, 11);
    dir->DIR_Attr = attr;
    dir->DIR_FstClusHI = 0;
    dir->DIR_FstClusLO = first_clus;
    dir->DIR_FileSize = file_size;

    struct timespec ts;
    int ret = clock_gettime(CLOCK_REALTIME, &ts);
    if (ret < 0) {
        return -errno;
    }
    time_unix_to_fat(&ts, &(dir->DIR_CrtDate), &(dir->DIR_CrtTime), &(dir->DIR_CrtTimeTenth));
    time_unix_to_fat(&ts, &(dir->DIR_WrtDate), &(dir->DIR_WrtTime), NULL);
    time_unix_to_fat(&ts, &(dir->DIR_LstAccDate), NULL, NULL);

    ret = dir_entry_write(slot);
    if (ret < 0) {
        return ret;
    }
    return 0;
}

/**
 * @brief 将 data 写入簇号为 clusterN 的簇对应的 FAT 表项，注意要对文件系统中所有 FAT 表都进行相同的写入。
 *
 * @param clus  要写入表项的簇号
 * @param data      要写入表项的数据，如下一个簇号，CLUSTER_END（文件末尾），或者 0（释放该簇）等等
 * @return int      成功返回 0
 */
int write_fat_entry(cluster_t clus, cluster_t data) {
    char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    size_t clus_off = clus * sizeof(cluster_t);
    sector_t clus_sec = clus_off / meta.sector_size;
    size_t sec_off = clus_off % meta.sector_size;
    for (size_t i = 0; i < meta.fats; i++) {
        // TODO: 2.2 修改第 i 个 FAT 表中，clus_sec 扇区中，sec_off 偏移处的表项，使其值为 data
        //   1. 计算第 i 个 FAT 表所在扇区，进一步计算 clus 应的 FAT 表项所在扇区
        //   2. 读取该扇区并在对应位置写入数据
        //   3. 将该扇区写回
    }
    return 0;
}

int free_clusters(cluster_t clus) {
    while (is_cluster_inuse(clus)) {
        cluster_t next = read_fat_entry(clus);
        int ret = write_fat_entry(clus, CLUSTER_FREE);
        if (ret < 0) {
            return ret;
        }
        clus = next;
    }
    return 0;
}

static const char ZERO_SECTOR[PHYSICAL_SECTOR_SIZE] = {0};
int cluster_clear(cluster_t clus) {
    sector_t first_sec = cluster_first_sector(clus);
    for (size_t i = 0; i < meta.sec_per_clus; i++) {
        sector_t sec = first_sec + i;
        int ret = sector_write(sec, ZERO_SECTOR);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

/**
 * @brief 分配 n 个空闲簇，分配过程中将 n 个簇通过 FAT 表项连在一起，然后返回第一个簇的簇号。
 *        最后一个簇的 FAT 表项将会指向 0xFFFF（即文件中止）。
 * @param fat16_ins 文件系统指针
 * @param n         要分配簇的个数
 * @return int      成功返回 0，失败返回错误代码负值
 */
int alloc_clusters(size_t n, cluster_t* first_clus) {
    if (n == 0)
        return CLUSTER_END;

    // 用于保存找到的 n 个空闲簇，另外在末尾加上 CLUSTER_END，共 n+1 个簇号
    cluster_t* clusters = malloc((n + 1) * sizeof(cluster_t));
    size_t allocated = 0;  // 已找到的空闲簇个数

    // TODO: 2.3 扫描 FAT 表，找到 n 个空闲的簇，存入 cluster 数组。注意此时不需要修改对应的 FAT 表项。
    // Hint: 你可以使用 read_fat_entry 函数来读取 FAT 表项的值，根据该值判断簇是否空闲。

    if (allocated != n) {  // 找不到 n 个簇，分配失败
        free(clusters);
        return -ENOSPC;
    }

    // 找到了 n 个空闲簇，将 CLUSTER_END 加至末尾。
    clusters[n] = CLUSTER_END;

    // TODO: 2.4 修改 clusters 中存储的 N 个簇对应的 FAT 表项，将每个簇与下一个簇连接在一起。同时清零每一个新分配的簇。
    // 清零要分配的簇
    for (size_t i = 0; i < n; i++) {
        int ret = cluster_clear(clusters[i]);  // 请实现 cluster_clear()
        if (ret < 0) {
            free(clusters);
            return ret;
        }
    }

    // TODO: 2.5 连接要分配的簇的 FAT 表项（Hint: 使用 write_fat_entry）
    // Hint: 将每个簇连接到下一个即可

    *first_clus = clusters[0];
    free(clusters);
    return 0;
}

/**
 * @brief 在 path 对应的路径创建新文件 （请阅读函数的逻辑，补全 find_empty_slot 和 dir_entry_create 两个函数）
 *
 * @param path    要创建的文件路径
 * @param mode    要创建文件的类型，本次实验可忽略，默认所有创建的文件都为普通文件
 * @param devNum  忽略，要创建文件的设备的设备号
 * @return int    成功返回 0，失败返回 POSIX 错误代码的负值
 */
int fat16_mknod(const char* path, mode_t mode, dev_t dev) {
    printf("mknod(path='%s', mode=%03o, dev=%lu)\n", path, mode, dev);
    DirEntrySlot slot;
    const char* filename = NULL;
    int ret = find_empty_slot(path, &slot, &filename);
    if (ret < 0) {
        return ret;
    }

    char shortname[11];
    ret = to_shortname(filename, MAX_NAME_LEN, shortname);
    if (ret < 0) {
        return ret;
    }
    // 这里创建文件时首簇号填了 0，你可以根据自己需要修改。
    ret = dir_entry_create(slot, shortname, ATTR_REGULAR, 0, 0);
    if (ret < 0) {
        return ret;
    }
    return 0;
}

/**
 * @brief 删除 path 对应的文件（请阅读函数逻辑，补全 free_clusters 和 dir_entry_write）
 *
 * @param path  要删除的文件路径
 * @return int  成功返回 0，失败返回 POSIX 错误代码的负值
 */
int fat16_unlink(const char* path) {
    printf("unlink(path='%s')\n", path);
    DirEntrySlot slot;
    DIR_ENTRY* dir = &(slot.dir);
    int ret = find_entry(path, &slot);
    if (ret < 0) {
        return ret;
    }
    if (is_directory(dir->DIR_Attr)) {
        return -EISDIR;
    }
    ret = free_clusters(dir->DIR_FstClusLO);
    if (ret < 0) {
        return ret;
    }
    dir->DIR_Name[0] = NAME_DELETED;
    ret = dir_entry_write(slot);
    if (ret < 0) {
        return ret;
    }
    return 0;
}

/**
 * @brief 修改 path 对应文件的时间戳，本次实验不做要求，可忽略该函数
 *
 * @param path  要修改时间戳的文件路径
 * @param tv    时间戳
 * @return int
 */
int fat16_utimens(const char* path, const struct timespec tv[2], struct fuse_file_info* fi) {
    printf("utimens(path='%s', tv=[%ld.%09ld, %ld.%09ld])\n", path,
           tv[0].tv_sec, tv[0].tv_nsec, tv[1].tv_sec, tv[1].tv_nsec);
    DirEntrySlot slot;
    DIR_ENTRY* dir = &(slot.dir);
    int ret = find_entry(path, &slot);
    if (ret < 0) {
        return ret;
    }

    time_unix_to_fat(&tv[1], &(dir->DIR_WrtDate), &(dir->DIR_WrtTime), NULL);
    time_unix_to_fat(&tv[0], &(dir->DIR_LstAccDate), NULL, NULL);
    ret = dir_entry_write(slot);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

/**
 * @brief 创建 path 对应的文件夹
 *
 * @param path 创建的文件夹路径
 * @param mode 文件模式，本次实验可忽略，默认都为普通文件夹
 * @return int 成功: 0， 失败: POSIX 错误代码的负值
 */
int fat16_mkdir(const char* path, mode_t mode) {
    // TODO: 2.6 参考 fat16_mknod 实现，创建新目录
    // Hint: 注意设置的属性不同。
    // Hint: 新目录最开始即有两个目录项，分别是. 和..，所以需要给新目录分配一个簇。
    // Hint: 你可以使用 alloc_clusters 来分配簇。

    const char DOT_NAME[] = ".";
    const char DOTDOT_NAME[] = "..";

    // TODO: 2.7 使用 dir_entry_create 创建 . 和 .. 目录项
    // Hint: 两个目录项分别在你刚刚分配的簇的前两项。
    // Hint: 记得修改下面的返回值。
    return -ENOSYS;
}

/**
 * @brief 删除 path 对应的文件夹
 *
 * @param path 要删除的文件夹路径
 * @return int 成功: 0， 失败: POSIX 错误代码的负值
 */
int fat16_rmdir(const char* path) {
    printf("rmdir(path='%s')\n", path);
    if (path_is_root(path)) {
        return -EBUSY;
    }

    DirEntrySlot slot;
    DIR_ENTRY* dir = &(slot.dir);
    int ret = find_entry(path, &slot);
    if (ret < 0) {
        return ret;
    }
    if (!is_directory(dir->DIR_Attr)) {
        return -ENOTDIR;
    }

    // TODO: 请参考 fat16_unlink 实现，实现删除目录功能。
    // Hint: 你只需要删除为空的目录，不为空的目录返回 -ENOTEMPTY 错误即可。
    // Hint: 你需要类似 fat16_readdir 一样，读取目录项，来判断目录是否为空。
    // Hint: 空目录中也有. 和.. 两个目录项，你要正确地忽略它们。
    // Hint: 记得修改下面的返回值

    return -ENOTSUP;
}

// ------------------TASK3: 写文件、裁剪文件 -----------------------------------

/**
 * @brief 将 data 中的数据写入编号为 clusterN 的簇的 offset 位置。
 *        注意 size+offset <= 簇大小
 *
 * @param fat16_ins 文件系统指针
 * @param clusterN  要写入数据的块号
 * @param data      要写入的数据
 * @param size      要写入数据的大小（字节）
 * @param offset    要写入簇的偏移量
 * @return ssize_t  成功写入的字节数，失败返回错误代码负值。可能部分成功，此时仅返回成功写入的字节数，不提供错误原因（POSIX 标准）。
 */
ssize_t write_to_cluster_at_offset(cluster_t clus, off_t offset, const char* data, size_t size) {
    assert(offset + size <= meta.cluster_size);  // offset + size 必须小于簇大小
    char sector_buffer[PHYSICAL_SECTOR_SIZE];

    size_t pos = 0;
    // TODO: 参考注释，以及 read_from_cluster_at_offset 函数，实现写入簇的功能。
    return pos;
}

/**
 * @brief 为文件分配新的簇至足够容纳 size 大小
 *
 * @param dir 文件的目录项
 * @param size 所需的大小
 * @return int 成功返回 0
 */
int file_reserve_clusters(DIR_ENTRY* dir, size_t size) {
    // TODO: 为文件分配新的簇至足够容纳 size 大小
    //   1. 计算需要多少簇
    //   2. 如果文件没有簇，直接分配足够的簇
    //   3. 如果文件已有簇，找到最后一个簇（哪个簇是当前该文件的最后一个簇？），并计算需要额外分配多少个簇
    //   4. 分配额外的簇，并将分配好的簇连在最后一个簇后
}

/**
 * @brief 将长度为 size 的数据 data 写入 path 对应的文件的 offset 位置。注意当写入数据量超过文件本身大小时，
 *        需要扩展文件的大小，必要时需要分配新的簇。
 *
 * @param path    要写入的文件的路径
 * @param data    要写入的数据
 * @param size    要写入数据的长度
 * @param offset  文件中要写入数据的偏移量（字节）
 * @param fi      本次实验可忽略该参数
 * @return int    成功返回写入的字节数，失败返回 POSIX 错误代码的负值。
 */
int fat16_write(const char* path, const char* data, size_t size, off_t offset, struct fuse_file_info* fi) {
    printf("write(path='%s', offset=%ld, size=%lu)\n", path, offset, size);
    // TODO: 写文件，请自行实现，将在下周发布进一步说明。
    return -ENOTSUP;
}

/**
 * @brief 将 path 对应的文件大小改为 size，注意 size 可以大于小于或等于原文件大小。
 *        若 size 大于原文件大小，需要将拓展的部分全部置为 0，如有需要，需要分配新簇。
 *        若 size 小于原文件大小，将从末尾截断文件，若有簇不再被使用，应该释放对应的簇。
 *        若 size 等于原文件大小，什么都不需要做。
 *
 * @param path 需要更改大小的文件路径
 * @param size 新的文件大小
 * @return int 成功返回 0，失败返回 POSIX 错误代码的负值。
 */
int fat16_truncate(const char* path, off_t size, struct fuse_file_info* fi) {
    printf("truncate(path='%s', size=%lu)\n", path, size);
    // TODO: 裁剪文件，请自行实现，将在下周发布说明。
    return -ENOTSUP;
}

struct fuse_operations fat16_oper = {
    .init = fat16_init,
    .destroy = fat16_destroy,
    .getattr = fat16_getattr,

    // TASK1: tree [dir] / ls [dir] ; cat [file] / tail [file] / head [file]
    .readdir = fat16_readdir,
    .read = fat16_read,

    // TASK2: touch [file]; rm [file]
    .mknod = fat16_mknod,
    .unlink = fat16_unlink,
    .utimens = fat16_utimens,

    // TASK3: mkdir [dir] ; rm -r [dir]
    .mkdir = fat16_mkdir,
    .rmdir = fat16_rmdir,

    // TASK4: echo "hello world!" > [file] ;  echo "hello world!" >> [file]
    .write = fat16_write,
    .truncate = fat16_truncate};
