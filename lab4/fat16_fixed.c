#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include "fat16.h"

static int fd;

struct disk_info {
    uint64_t seek_time_us;      // 磁头移动一个磁道所需时间
    long last_track;
    long total_track;
};
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static struct disk_info di;

void busywait(long us) {
    struct timespec s, t;
    clock_gettime(CLOCK_MONOTONIC, &s);
    while (true) {
        clock_gettime(CLOCK_MONOTONIC, &t);
        long dus = (long)(t.tv_sec - s.tv_sec) * 1000000 + (t.tv_nsec - s.tv_nsec) / 1000;
        if (dus>= us) {
            break;
        }
        if (dus < 0) {
            printf("fatal: time error\n");
            break;
        }
    }
}

void seek_to(sector_t sec) {
    long track = sec / SEC_PER_TRACK;
    long delta = labs(track - di.last_track);
    busywait(delta * di.seek_time_us);
    di.last_track = track;
}

int sector_read(sector_t sec_num, void *buffer) {
    if(pthread_mutex_lock(&mutex) != 0) {
        printf("read sector %lu error: lock failed.\n", sec_num);
        return 1;
    }
    seek_to(sec_num);
    ssize_t ret = pread(fd, buffer, PHYSICAL_SECTOR_SIZE, sec_num * PHYSICAL_SECTOR_SIZE);
    pthread_mutex_unlock(&mutex);
    if(ret != PHYSICAL_SECTOR_SIZE) {
        printf("read sector %lu error: image read failed.\n", sec_num);
        return 1;
    }
    return 0;
}

int sector_write(sector_t sec_num, const void *buffer) {
    if(pthread_mutex_lock(&mutex) != 0) {
        printf("write sector %lu error: lock failed.\n", sec_num);
        return 1;
    }
    seek_to(sec_num);
    ssize_t ret = pwrite(fd, buffer, PHYSICAL_SECTOR_SIZE, sec_num * PHYSICAL_SECTOR_SIZE);
    pthread_mutex_unlock(&mutex);
    if(ret != PHYSICAL_SECTOR_SIZE) {
        printf("write sector %lu error: image write failed.\n", sec_num);
        return 1;
    }
    return 0;
}

void init_disk(const char* path, uint64_t seek_time_ns) {
    fd = open(path, O_RDWR | O_DSYNC);
    if(fd < 0) {
        fprintf(stderr, "Open image file %s failed: %s\n", path, strerror(errno));
        exit(ENOENT);
    }
    di.seek_time_us = seek_time_ns;
    di.last_track = 0;
    di.total_track = lseek(fd, 0, SEEK_END) / PHYSICAL_SECTOR_SIZE / SEC_PER_TRACK;
}

typedef struct {
    const char* image_path;
    uint64_t seek_time_us;
} Options;

#define OPTION(t, p) { t, offsetof(Options, p), 1 }
static const struct fuse_opt option_spec[] = {
    OPTION("--img=%s", image_path),
    OPTION("--seek_time=%lu", seek_time_us),
    FUSE_OPT_END
};

extern struct fuse_operations fat16_oper;

int main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    Options opts;
    opts.image_path = strdup(DEFAULT_IMAGE);
    opts.seek_time_us = 0;
    int ret = fuse_opt_parse(&args, &opts, option_spec, NULL);
    if(ret < 0) {
        return EXIT_FAILURE;
    }
    init_disk(opts.image_path, opts.seek_time_us);
    ret = fuse_main(args.argc, args.argv, &fat16_oper, NULL);
    fuse_opt_free_args(&args);
    return ret;
}