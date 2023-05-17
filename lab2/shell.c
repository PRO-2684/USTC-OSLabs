#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_CMDLINE_LENGTH 1024 /* max cmdline length in a line*/
#define MAX_BUF_SIZE 4096       /* max buffer size */
#define MAX_CMD_ARG_NUM 32      /* max number of single command args */
#define MAX_PATH_LENGTH 128     /* max length of path */
#define WRITE_END 1             // pipe write end
#define READ_END 0              // pipe read end
#define HOME "/home/ubuntu/"

/// @brief 基于分隔符sep对于string做分割，并去掉头尾的空格
/// @param string 输入, 待分割的字符串
/// @param sep 输入, 分割符
/// @param string_clips 输出, 分割好的字符串数组
/// @return 分割的段数
int split_string(char* string, char* sep, char** string_clips) {
    char string_dup[MAX_BUF_SIZE]; // 分割结果数组
    string_clips[0] = strtok(string, sep); // 第一个 token
    int clip_num = 0; // 当前处理第几个 token

    do {
        char *head, *tail;
        head = string_clips[clip_num]; // 当前 token 的第一个字符
        tail = head + strlen(string_clips[clip_num]) - 1; // 当前 token 的末尾字符
        // 去除前后空格
        while (*head == ' ' && head != tail)
            head++;
        while (*tail == ' ' && tail != head)
            tail--;
        *(tail + 1) = '\0';
        string_clips[clip_num] = head;
        clip_num++;
    } while (string_clips[clip_num] = strtok(NULL, sep)); // 继续处理后一个 token
    return clip_num;
}

/// @brief 执行内置命令: `cd`, `exit`, `kill`
/// @param argc 输入，命令的参数个数
/// @param argv 输入，依次代表每个参数，注意第一个参数就是要执行的命令，若执行 `ls a b c` 命令，则argc=4, argv={"ls", "a", "b", "c"}
/// @param fd 输出，命令输入和输出的文件描述符 (Deprecated)
/// @return 若执行成功返回0，否则返回值非零
int exec_builtin(int argc, char** argv, int* fd) {
    if (argc == 0) {
        return 0;
    }
    if (strcmp(argv[0], "cd") == 0) { // cd <PATH>
        if (argc == 1 || argc == 2 && (strcmp(argv[1], "~") == 0)) { // `cd`
            chdir(HOME);
        } else { // cd <PATH>
            chdir(argv[1]);
        }
        return 0;
    } else if (strcmp(argv[0], "exit") == 0) {
        // exit [n] (Exits the shell with a status of n, default 0.)
        if (argc == 1) { // No status value specified
            exit(EXIT_SUCCESS); // Default to SUCCESS
        } else {
            exit(atoi(argv[1])); // Use specified value
        }
        return 0;
    } else if (strcmp(argv[0], "kill") == 0) {
        // kill: kill <PID> [SIG] (Send SIG (int, default SIGTERM) to process PID)
        if (argc == 1) { // No pid specified
            puts("\033[32mkill: usage: kill PID [SIG]\033[0m");
            return 0;
        }
        pid_t pid = atoi(argv[1]);
        int signal = SIGTERM;
        if (argc >= 3) { // User specified signal
            signal = atoi(argv[2]);
        }
        kill(pid, signal);
        return 0;
    } else {
        // 不是内置指令
        return -1;
    }
}

/// @brief 从argv中删除重定向符和随后的参数，并打开对应的文件，将文件描述符放在fd数组中。运行后，fd[0]读端的文件描述符，fd[1]是写端的文件描述符
/// @param argc 输入，命令的参数个数
/// @param argv 输入，依次代表每个参数，注意第一个参数就是要执行的命令，若执行 `ls a b c` 命令，则argc=4, argv={"ls", "a", "b", "c"}
/// @param fd 输出，命令输入和输出使用的文件描述符
/// @return 返回处理过重定向后命令的参数个数
int process_redirect(int argc, char** argv, int* fd) {
    /* 默认输入输出到命令行，即输入STDIN_FILENO，输出STDOUT_FILENO */
    fd[READ_END] = STDIN_FILENO;
    fd[WRITE_END] = STDOUT_FILENO;
    int i = 0, j = 0;
    while (i < argc) {
        int tfd;
        if (strcmp(argv[i], ">") == 0) { // 输出文件从头写入
            tfd = open(argv[i + 1], O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
            if (tfd < 0) {
                printf("\033[31mopen '%s' error: %s\033[0m\n", argv[i + 1], strerror(errno));
            } else {
                fd[WRITE_END] = tfd; // 输出重定向
            }
            i += 2;
        } else if (strcmp(argv[i], ">>") == 0) { // 输出文件追加写入
            tfd = open(argv[i + 1], O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
            if (tfd < 0) {
                printf("open '%s' error: %s\n", argv[i + 1], strerror(errno));
            } else {
                fd[WRITE_END] = tfd; // 输出重定向
            }
            i += 2;
        } else if (strcmp(argv[i], "<") == 0) { // 读输入文件
            tfd = open(argv[i + 1], O_RDONLY);
            if (tfd < 0) {
                printf("\033[31mopen '%s' error: %s\033[0m\n", argv[i + 1], strerror(errno));
            } else {
                fd[READ_END] = tfd; // 输入重定向
            }
            i += 2;
        } else {
            argv[j++] = argv[i++];
        }
    }
    argv[j] = NULL;
    return j;  // 新的argc
}

/// @brief 在本进程中执行，且执行完毕后结束进程。
/// @param argc 命令的参数个数
/// @param argv 依次代表每个参数，注意第一个参数就是要执行的命令，若执行 `ls a b c` 命令，则argc=4, argv={"ls", "a", "b", "c"}
/// @return 若执行成功则不会返回（进程直接结束），否则返回非零
int execute(int argc, char** argv) {
    int fd[2];
    // 默认输入输出到命令行，即输入STDIN_FILENO，输出STDOUT_FILENO
    fd[READ_END] = STDIN_FILENO;
    fd[WRITE_END] = STDOUT_FILENO;
    // 处理重定向符，如果不做本部分内容，请注释掉process_redirect的调用
    argc = process_redirect(argc, argv, fd);
    // 保存 stdin, stdout
    int stdin_ = dup(STDIN_FILENO);
    int stdout_ = dup(STDOUT_FILENO);
    // 重定向
    dup2(fd[READ_END], STDIN_FILENO);
    dup2(fd[WRITE_END], STDOUT_FILENO);
    if (exec_builtin(argc, argv, fd) == 0) { // 是内建指令
        exit(0);
    }
    // 运行命令与结束
    execvp(argv[0], argv);
    // 恢复 stdin, stdout
    close(fd[READ_END]);
    close(fd[WRITE_END]);
    dup2(stdin_, STDIN_FILENO);
    dup2(stdout_, STDOUT_FILENO);
    printf("\033[31mError: command %s not found!\033[0m\n", argv[0]);
    exit(255);
}

int main() {
    /* 输入的命令行 */
    char cmdline[MAX_CMDLINE_LENGTH];
    char* commands[128];
    char* sub_commands[128];
    char path_buf[MAX_PATH_LENGTH];
    int cmd_count;
    while (1) {
        // 增加打印当前目录，格式类似"shell:/home/oslab ->"，你需要改下面的printf
        getcwd(path_buf, MAX_PATH_LENGTH * sizeof(char));
        // printf("PRO@USTC:%s$ ", path_buf);
        printf("\033[1;36mPRO@USTC\033[0m:\033[4;37m%s\033[0m$ ", path_buf);
        fflush(stdout);

        if (fgets(cmdline, 256, stdin) == NULL) { // 终止输入
            putchar('\n');
            break;
        }
        strtok(cmdline, "\n");

        // 由 ; 分隔的指令
        int sub_cmd_count = split_string(cmdline, ";", commands);
        int i = 0;
        // 基于";"的多命令执行
        while (i < sub_cmd_count) {
            // 由管道操作符'|'分割的命令行各个部分，每个部分是一条命令
            // 拆解命令行
            cmd_count = split_string(commands[i++], "|", sub_commands);

            if (cmd_count == 0) {
                continue;
            } else if (cmd_count == 1) {  // 没有管道的单一命令
                char* argv[MAX_CMD_ARG_NUM];
                int argc;
                int fd[2];
                // 处理参数，分出命令名和参数
                argc = split_string(sub_commands[0], " ", argv);
                argc = process_redirect(argc, argv, fd);
                // 保存 stdin, stdout
                int stdin_ = dup(STDIN_FILENO);
                int stdout_ = dup(STDOUT_FILENO);
                // 重定向
                dup2(fd[READ_END], STDIN_FILENO);
                dup2(fd[WRITE_END], STDOUT_FILENO);
                // 在没有管道时，内建命令直接在主进程中完成，外部命令通过创建子进程完成
                if (exec_builtin(argc, argv, fd) != 0) { // 不是内建指令
                    // 创建子进程，运行命令，等待命令运行结束
                    int pid = fork();
                    if (pid == 0) { // 子进程
                        execvp(argv[0], argv);
                        // 恢复 stdin, stdout
                        dup2(stdin_, STDIN_FILENO);
                        dup2(stdout_, STDOUT_FILENO);
                        printf("\033[31mError: command \"%s\" not found!\033[0m\n", argv[0]);
                        exit(255);
                    } else if (pid == -1) { // fork 失败
                        puts("\033[31mFork error!\033[0m");
                    } else { // 父进程
                        while (wait(NULL) > 0)
                            ; // 等待子进程
                    }
                }
                // 恢复 stdin, stdout
                close(fd[READ_END]);
                close(fd[WRITE_END]);
                dup2(stdin_, STDIN_FILENO);
                dup2(stdout_, STDOUT_FILENO);
                continue;

            } else if (cmd_count == 2) {  // 两个命令间的管道
                int pipefd[2];
                int ret = pipe(pipefd);
                if (ret < 0) {
                    printf("\033[31mPipe error!\033[0m\n");
                    continue;
                }
                // 子进程1
                int pid = fork();
                if (pid == 0) {
                    // 子进程1 将标准输出重定向到管道，注意这里数组的下标被挖空了要补全
                    close(pipefd[READ_END]);
                    dup2(pipefd[WRITE_END], STDOUT_FILENO);
                    /*
                        在使用管道时，为了可以并发运行，所以内建命令也在子进程中运行
                        因此我们用了一个封装好的execute函数
                    */
                    char* argv[MAX_CMD_ARG_NUM];
                    int argc = split_string(sub_commands[0], " ", argv);
                    execute(argc, argv);
                    // close(pipefd[WRITE_END]);
                    printf("\033[31mError: command \"%s\" not found!\033[0m", argv[0]);
                    exit(255);
                }
                // 因为在shell的设计中，管道是并发执行的，所以我们不在每个子进程结束后才运行下一个，而是直接创建下一个子进程
                // 子进程2
                pid = fork();
                if (pid == 0) {
                    // 子进程2 将标准输入重定向到管道，注意这里数组的下标被挖空了要补全
                    close(pipefd[WRITE_END]);
                    dup2(pipefd[READ_END], STDIN_FILENO);

                    char* argv[MAX_CMD_ARG_NUM];
                    int argc = split_string(sub_commands[1], " ", argv);
                    execute(argc, argv);
                    // close(pipefd[READ_END]);
                    printf("\033[31mError: command \"%s\" not found!\033[0m", argv[0]);
                    exit(255);
                }
                close(pipefd[WRITE_END]);
                close(pipefd[READ_END]);

                while (wait(NULL) > 0)
                    ;
            } else { // 选做：三个以上的命令
                puts("Not implemented.");
                continue;
            }
        }
    }
}
