// #ifdef __INTELLISENSE__
//     #pragma diag_suppress 91
//     #pragma diag_suppress 145
// #endif
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include <linux/atomic.h>
#include <linux/freezer.h>
#include <linux/hashtable.h>
#include <linux/mmzone.h>
#include <linux/percpu.h>
#include <linux/types.h>
#include <linux/vm_event_item.h>

#include <asm/pgtable.h>
#include <asm/segment.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/sched/mm.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/vmstat.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OS2023");
MODULE_DESCRIPTION("KTEST!");
MODULE_VERSION("1.0");

// 定义要输出的文件
#define OUTPUT_FILE "/home/yll/lab3/part2/expr_result.txt"
struct file* log_file = NULL;
char str_buf[64];     // 暂存数据
char buf[PAGE_SIZE];  // 全局变量，用来缓存要写入到文件中的内容
// 记录当前写到 buf 的哪一个位置
size_t curr_buf_length = 0;  // 缓冲偏移

typedef typeof(page_referenced)* my_page_referenced;
typedef typeof(follow_page)* my_follow_page;
// typedef typeof(follow_page_mask) *my_follow_page_mask;

// sudo cat /proc/kallsyms | grep page_referenced
static my_page_referenced mpage_referenced = (my_page_referenced)0xffffffffa06b0780;
// sudo cat /proc/kallsyms | grep follow_page
static my_follow_page mfollow_page = (my_follow_page)0xffffffffa0694160;
// follow_page 在具体实现时会调用 follow_page_mask 函数。
// 在不同的内核版本中，follow_page 不一定可以被访问。
// 经测试发现，在 Linux 4.9.263 中，无法使用 follow_page 函数，但是可以使用 follow_page_mask
// 同学们如果在执行 "sudo cat /proc/kallsyms | grep follow_page" 无法得到 follow_page 的地址时
// 可以考虑使用 follow_page_mask 函数
// sudo cat /proc/kallsyms | grep follow_page_mask
// static mfollow_page_mask = (my_follow_page_mask)0xffffffff85e93dd0;

// 进程的 pid
static unsigned int pid = 0;

// sysfs
#define KTEST_RUN_STOP 0
#define KTEST_RUN_START 1

// /sys/kernel/mm/ktest/func
static unsigned int ktest_func = 0;
// /sys/kernel/mm/ktest/ktest_run
static unsigned int ktest_run = KTEST_RUN_STOP;
// /sys/kernel/mm/ktest/sleep_millisecs
static unsigned int ktest_thread_sleep_millisecs = 5000;

static struct task_struct* ktest_thread;
static DECLARE_WAIT_QUEUE_HEAD(ktest_thread_wait);

// 保护 kpap_run 变量
static DEFINE_MUTEX(ktest_thread_mutex);

static struct task_info my_task_info;

// 统计信息
struct task_cnt {
    unsigned long active;
    unsigned long inactive;
};

struct task_info {
    struct task_struct* task;
    unsigned int vma_cnt;
    struct task_cnt page_cnt;
};

static inline void write_to_file(void* buffer, size_t length) {
#ifdef get_fs
    mm_segment_t old_fs;
    old_fs = get_fs();
    set_fs(KERNEL_DS);
#endif
    kernel_write(log_file, (char*)buffer, length, &log_file->f_pos);
    // vfs_write(log_file, (char *)buffer, length, &log_file->f_pos);
#ifdef get_fs
    set_fs(old_fs);
#endif
}

// 将全局变量 buf 中的内容写入到文件中
static void flush_buf(int end_flag) {
    if (IS_ERR(log_file)) {
        printk(KERN_ERR "error when flush_buf %s, exit\n", OUTPUT_FILE);
        return;
    }

    if (end_flag == 1) {
        if (likely(curr_buf_length> 0))
            buf[curr_buf_length - 1] = '\n';
        else
            buf[curr_buf_length++] = '\n';
    }
    write_to_file(buf, curr_buf_length);

    curr_buf_length = 0;
    memset(buf, 0, PAGE_SIZE);
}

// 写一个 unsigned long 型的数据到全局变量 buf 中
static void record_one_data(unsigned long data) {
    sprintf(str_buf, "%lu,", data);
    sprintf(buf + curr_buf_length, "%lu,", data);
    curr_buf_length += strlen(str_buf);
    if ((curr_buf_length + (sizeof(unsigned long) + 2)) >= PAGE_SIZE)
        flush_buf(0);
}

// 写两个 unsigned long 中的数据到全局变量 buf 中
static void record_two_data(unsigned long data1, unsigned long data2) {
    sprintf(str_buf, "0x%lx--0x%lx\n", data1, data2);
    sprintf(buf + curr_buf_length, "0x%lx--0x%lx\n", data1, data2);
    curr_buf_length += strlen(str_buf);
    if ((curr_buf_length + (sizeof(unsigned long) + 2)) >= PAGE_SIZE)
        flush_buf(0);
}

// func == 1
static void scan_vma(void) {
    printk("func == 1, %s\n", __func__);
    struct mm_struct* mm = get_task_mm(my_task_info.task);
    if (mm) {
        // FIXME: 遍历 VMA 将 VMA 的个数记录到 my_task_info 的 vma_cnt 变量中
        int cnt = 0;
        struct vm_area_struct* node = mm->mmap;
        while (node != NULL) {
            cnt++;
            node = node->vm_next;
        }
        my_task_info.vma_cnt = cnt;
        mmput(mm);
    }
}

// func == 2
static void print_mm_active_info(void) {
    printk("func == 2, %s\n", __func__);
    // TODO: 1. 遍历 VMA，并根据 VMA 的虚拟地址得到对应的 struct page 结构体（使用 mfollow_page 函数）
    // struct page *page = mfollow_page(vma, virt_addr, FOLL_GET);
    // unsigned int unused_page_mask;
    // struct page *page = mfollow_page_mask(vma, virt_addr, FOLL_GET, &unused_page_mask);
    // TODO: 2. 使用 page_referenced 活跃页面是否被访问，并将被访问的页面物理地址写到文件中
    // kernel v5.13.0-40 及之后可尝试
    // unsigned long vm_flags;
    // int freq = mpage_referenced(page, 0, (struct mem_cgroup *)(page->memcg_data), &vm_flags);
    // kernel v5.9.0
    // unsigned long vm_flags;
    // int freq = mpage_referenced(page, 0, page->mem_cgroup, &vm_flags);
}

static unsigned long virt2phys(struct mm_struct* mm, unsigned long virt) {
    struct page* page = NULL;
    // TODO: 多级页表遍历：pgd->pud->pmd->pte，然后从 pte 到 page，最后得到 pfn
    if (page) {
        return page_to_pfn(page);
    } else {
        pr_err("func: %s page is NULL\n", __func__);
        return NULL;
    }
}

// func = 3
static void traverse_page_table(struct task_struct* task) {
    printk("func == 3, %s\n", __func__);
    struct mm_struct* mm = get_task_mm(my_task_info.task);
    if (mm) {
        // TODO: 遍历 VMA，并以 PAGE_SIZE 为粒度逐个遍历 VMA 中的虚拟地址，然后进行页表遍历
        record_two_data(virt_addr, virt2phys(task->mm, virt_addr));
        mmput(mm);
    } else {
        pr_err("func: %s mm_struct is NULL\n", __func__);
    }
}

// func == 4 或者 func == 5
static void print_seg_info(void) {
    struct mm_struct* mm;
    unsigned long addr;
    printk("func == 4 or func == 5, %s\n", __func__);
    mm = get_task_mm(my_task_info.task);
    if (mm == NULL) {
        pr_err("mm_struct is NULL\n");
        return;
    }
    // TODO: 根据数据段或者代码段的起始地址和终止地址得到其中的页面，然后打印页面内容到文件中
    // 相关提示：可以使用 follow_page 函数得到虚拟地址对应的 page，然后使用 addr=kmap_atomic(page) 得到可以直接
    //          访问的虚拟地址，然后就可以使用 memcpy 函数将数据段或代码段拷贝到全局变量 buf 中以写入到文件中
    //          注意：使用 kmap_atomic(page) 结束后还需要使用 kunmap_atomic(addr) 来进行释放操作
    //          正确结果：如果是运行实验提供的 workload，这一部分数据段应该会打印出 char *trace_data，
    //                   static char global_data[100] 和 char hamlet_scene1[8276] 的内容。
    mmput(mm);
}

// 检查程序是否应该停止
static int ktestd_should_run(void) {
    return (ktest_run & KTEST_RUN_START);
}

// 根据进程 PID 得到该 PID 对应的进程描述符 task_struct
static struct task_struct* pid_to_task(int pid_n) {
    struct pid* pid;
    struct task_struct* task;
    if (pid_n != -1) {
        printk("pid_n receive successfully:%d!\n", pid_n);
    } else {
        printk("error:pid_n receive nothing!\n");
        return NULL;
    }
    cond_resched();
    pid = find_get_pid(pid_n);
    task = pid_task(pid, PIDTYPE_PID);
    return task;
}

// 用于确定内核线程周期性要做的事情。实验要求的 5 个 func 在此函数中被调用
static void ktest_to_do(void) {
    my_task_info.vma_cnt = 0;
    memset(&(my_task_info.page_cnt), 0, sizeof(struct task_cnt));
    switch (ktest_func) {
        case 0:
            printk("hello world!");
            break;
        case 1:
            // 扫描 VMA，并进行计数
            scan_vma();
            break;
        case 2:
            // 打印页面活跃度信息，并输出到文件中
            print_mm_active_info();
            break;
        case 3:
            // 遍历多级页表得到虚拟地址对应的物理地址
            traverse_page_table(my_task_info.task);
            break;
        case 4:
        case 5:
            // 打印数据的代码段和数据段内容
            print_seg_info();
            break;
        default:
            pr_err("ktest func args error\n");
    }
}

static int ktestd_thread(void* nothing) {
    set_freezable();
    set_user_nice(current, 5);
    // 一直判断 ktestd_thread 是否应该运行，根据用户对 ktestrun 的指定
    while (!kthread_should_stop()) {
        mutex_lock(&ktest_thread_mutex);
        if (ktestd_should_run())  // 如果 ktestd_thread 应该处于运行状态
        {
            // 判断文件描述符是否为 NULL
            if (IS_ERR_OR_NULL(log_file)) {
                // 打开文件
                log_file = filp_open(OUTPUT_FILE, O_RDWR | O_APPEND | O_CREAT, 0666);
                if (IS_ERR_OR_NULL(log_file)) {
                    pr_err("Open file %s error, exit\n", OUTPUT_FILE);
                    goto next_loop;
                }
            }
            // 调用 ktest_to_do 函数，实际上就是根据用户指定的 func 参数来完成相应的任务
            ktest_to_do();
        }
    next_loop:
        mutex_unlock(&ktest_thread_mutex);
        try_to_freeze();
        if (ktestd_should_run()) {
            // 周期性 sleep
            schedule_timeout_interruptible(
                msecs_to_jiffies(ktest_thread_sleep_millisecs));
        } else {
            // 挂起线程
            wait_event_freezable(ktest_thread_wait,
                                 ktestd_should_run() || kthread_should_stop());
        }
    }
    return 0;
}
// sysfs 文件相关的函数，无需改动。
#ifdef CONFIG_SYSFS
/*
 * This all compiles without CONFIG_SYSFS, but is a waste of space.
 */
#define KPAP_ATTR_RO(_name) \
    static struct kobj_attribute _name##_attr = __ATTR_RO(_name)

#define KPAP_ATTR(_name)                        \
    static struct kobj_attribute _name##_attr = \
        __ATTR(_name, 0644, _name##_show, _name##_store)

static ssize_t sleep_millisecs_show(struct kobject* kobj,
                                    struct kobj_attribute* attr,
                                    char* buf) {
    return sprintf(buf, "%u\n", ktest_thread_sleep_millisecs);
}

static ssize_t sleep_millisecs_store(struct kobject* kobj,
                                     struct kobj_attribute* attr,
                                     const char* buf,
                                     size_t count) {
    unsigned long msecs;
    int err;

    err = kstrtoul(buf, 10, &msecs);
    if (err || msecs> UINT_MAX)
        return -EINVAL;

    ktest_thread_sleep_millisecs = msecs;

    return count;
}
KPAP_ATTR(sleep_millisecs);

static ssize_t pid_show(struct kobject* kobj,
                        struct kobj_attribute* attr,
                        char* buf) {
    return sprintf(buf, "%u\n", pid);
}

static ssize_t pid_store(struct kobject* kobj,
                         struct kobj_attribute* attr,
                         const char* buf,
                         size_t count) {
    unsigned long tmp;
    int err;

    err = kstrtoul(buf, 10, &tmp);
    if (err || tmp> UINT_MAX)
        return -EINVAL;

    if (pid != tmp) {
        pid = tmp;
        // 根据用户传入的 PID 获取进程的任务描述符（即 task_struct）信息
        my_task_info.task = pid_to_task(pid);
    }
    return count;
}
KPAP_ATTR(pid);

static ssize_t func_show(struct kobject* kobj,
                         struct kobj_attribute* attr,
                         char* buf) {
    return sprintf(buf, "%u\n", ktest_func);
}

static ssize_t func_store(struct kobject* kobj,
                          struct kobj_attribute* attr,
                          const char* buf,
                          size_t count) {
    unsigned long tmp;
    int err;

    err = kstrtoul(buf, 10, &tmp);
    if (err || tmp> UINT_MAX)
        return -EINVAL;

    ktest_func = tmp;
    printk("ktest func=%d\n", ktest_func);
    return count;
}
KPAP_ATTR(func);

static ssize_t vma_show(struct kobject* kobj,
                        struct kobj_attribute* attr,
                        char* buf) {
    return sprintf(buf, "%d, %u\n", pid, my_task_info.vma_cnt);
}

static ssize_t vma_store(struct kobject* kobj,
                         struct kobj_attribute* attr,
                         const char* buf,
                         size_t count) {
    unsigned long tmp;
    int err;

    err = kstrtoul(buf, 10, &tmp);
    if (err || tmp> UINT_MAX)
        return -EINVAL;
    printk("not supported to set vma count!");

    return count;
}
KPAP_ATTR(vma);

static ssize_t ktestrun_show(struct kobject* kobj, struct kobj_attribute* attr, char* buf) {
    return sprintf(buf, "%u\n", ktest_run);
}

static ssize_t ktestrun_store(struct kobject* kobj, struct kobj_attribute* attr, const char* buf, size_t count) {
    int err;
    unsigned long flags;
    err = kstrtoul(buf, 10, &flags);
    if (err || flags> UINT_MAX)
        return -EINVAL;
    if (flags> KTEST_RUN_START)
        return -EINVAL;
    mutex_lock(&ktest_thread_mutex);
    if (ktest_run != flags) {
        ktest_run = flags;
        if (ktest_run == KTEST_RUN_STOP) {
            pid = 0;
        }
    }
    mutex_unlock(&ktest_thread_mutex);
    // 如果用户要求运行 ktestd_thread 线程，在唤醒被挂起的 ktestd_thread
    if (flags & KTEST_RUN_START)
        wake_up_interruptible(&ktest_thread_wait);
    return count;
}
KPAP_ATTR(ktestrun);

static struct attribute* ktest_attrs[] = {
    &sleep_millisecs_attr.attr,
    &pid_attr.attr,
    &func_attr.attr,
    &ktestrun_attr.attr,
    &vma_attr.attr,
    NULL,
};

static struct attribute_group ktest_attr_group = {
    .attrs = ktest_attrs,
    .name = "ktest",
};
#endif /* CONFIG_SYSFS */

static int __init ktest_init(void) {
    int err;
    // 创建一个内核线程，线程要做的事情在 ktestd_thread
    ktest_thread = kthread_run(ktestd_thread, NULL, "ktest");
    if (IS_ERR(ktest_thread)) {
        pr_err("ktest: creating kthread failed\n");
        err = PTR_ERR(ktest_thread);
        goto out;
    }

#ifdef CONFIG_SYSFS
    // 创建 sysfs 系统，并将其挂载到 / sys/kernel/mm / 下
    err = sysfs_create_group(mm_kobj, &ktest_attr_group);
    if (err) {
        pr_err("ktest: register sysfs failed\n");
        kthread_stop(ktest_thread);
        goto out;
    }
#else
    ktest_run = KSCAN_RUN_STOP;
#endif /* CONFIG_SYSFS */
    printk("ktest_init successful\n");
out:
    return err;
}
module_init(ktest_init);

static void __exit ktest_exit(void) {
    if (ktest_thread) {
        kthread_stop(ktest_thread);
        ktest_thread = NULL;
    }
    if (log_file != NULL)
        filp_close(log_file, NULL);
#ifdef CONFIG_SYSFS
    sysfs_remove_group(mm_kobj, &ktest_attr_group);
#endif
    printk("ktest exit success!\n");
}
module_exit(ktest_exit);