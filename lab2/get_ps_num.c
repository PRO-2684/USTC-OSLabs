#include <stdio.h>          // I/O
#include <unistd.h>         // System library
#include <sys/syscall.h>    // Invoking custom syscalls
#include <getopt.h>         // Commandline arguments parsing
#include <stdlib.h>         // Str to int

#define NAME_MAX 255
#define PROCESS_MAX 128
#define PID_MAX 4096
#define UNIT_CONV 1e9

struct process_info {
    int pid;
    unsigned long uptime;
    short state;
    char name[NAME_MAX];
};

int main(int argc, char* argv[]) {
    // Parsing commandline arguments
    int opt;
    char *opt_str = "d:";
    unsigned int interval; // Refresh interval
    // We only need to test argument "-d", so no loop needed
    opt = getopt(argc, argv, opt_str);
    if (opt == -1) {
        interval = 1;
    } else {
        interval = atoi(optarg);
    }

    // Custom syscall #1: get_ps_num
    int res = 0; // Process count
    syscall(332, &res);
    printf("\033[1;36mCurrent process count: %d\033[0m\nPress enter to continue...", res);
    getchar();

    // Custom syscall #2: top
    struct process_info tasks[PROCESS_MAX]; // All processes
    int sorted[PROCESS_MAX]; // Mapping used for sorting
    double cpu[PROCESS_MAX]; // Cpu usage of processes
    unsigned long last_uptime[PID_MAX] = {0}; // Identify last_uptime by **pid**
    int pid, max_idx, tmp;
    double cpu_max;
    syscall(333, &res, &tasks);
    while (1) {
        for (int i = 0; i < res; i++) {
            pid = tasks[i].pid;
            // Calculate cpu usage
            cpu[i] = (tasks[i].uptime - last_uptime[pid]) / (interval * UNIT_CONV);
            last_uptime[pid] = tasks[i].uptime;
            sorted[i] = i;
        }
        printf("\033c"); // Clear screen
        puts("\033[1;36mPID\tCOMM    \tRUNNING\t\%CPU      \tUPTIME\033[0m");
        // puts("\033[1;33m#\tPID\t\%CPU      \tUPTIME\033[0m"); // DEBUG
        // Sorting
        for (int i = 0; i < 20; i++) {
            // Sort "sorted" array
            cpu_max = 0.0;
            max_idx = i;
            for (int j = i; j < res; j++) {
                if (cpu[sorted[j]] > cpu_max) {
                    max_idx = j;
                }
            }
            // Swap
            tmp = sorted[max_idx];
            sorted[max_idx] = sorted[i];
            sorted[i] = tmp;
            printf("%d\t%s\t%d\t%f\t%f\n", tasks[tmp].pid, tasks[tmp].name, tasks[tmp].state == 0, cpu[tmp] * 100, tasks[tmp].uptime / UNIT_CONV);
            // printf("#%d\t%d\t%f\t%f\n", tmp, tasks[tmp].pid, cpu[tmp], tasks[tmp].uptime / UNIT_CONV); // Debug
        }
        // Get info via syscall
        syscall(333, &res, &tasks);
        sleep(interval);
    }
    return 0;
}