#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_ARGS 100       // 参数最大数量
#define MAX_LINE 1024      // 输入行最大长度

char prev_dir[1024] = "";  // 记录上一次所在目录，用于 cd -

// SIGCHLD 信号处理器：清理后台子进程，避免僵尸进程
void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

// 显示提示符：显示当前路径
void print_prompt() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s$ ", cwd);
    } else {
        perror("getcwd");
    }
    fflush(stdout);
}

// 解析命令：按空格分割参数，识别 &, <, >, | 等
void parse_command(char *line, char **args, int *background, char **infile, char **outfile, int *pipe_index) {
    *background = 0;
    *infile = NULL;
    *outfile = NULL;
    *pipe_index = -1;
    int i = 0;

    char *token = strtok(line, " \t\n");
    while (token != NULL && i < MAX_ARGS - 1) {
        if (strcmp(token, "&") == 0) {
            *background = 1;
        } else if (strcmp(token, "<") == 0) {
            token = strtok(NULL, " \t\n");
            *infile = token;
        } else if (strcmp(token, ">") == 0) {
            token = strtok(NULL, " \t\n");
            *outfile = token;
        } else if (strcmp(token, "|") == 0) {
            args[i++] = NULL;
            *pipe_index = i;
        } else {
            args[i++] = token;
        }
        token = strtok(NULL, " \t\n");
    }
    args[i] = NULL;
}

// 判断是否是内建命令
int is_builtin(char *cmd) {
    return strcmp(cmd, "cd") == 0 || strcmp(cmd, "exit") == 0 || strcmp(cmd, "export") == 0;
}

// 执行内建命令：cd、exit、export
void exec_builtin(char **args) {
    if (strcmp(args[0], "cd") == 0) {
        char cwd[1024];
        getcwd(cwd, sizeof(cwd));

        if (args[1] == NULL || strcmp(args[1], "~") == 0) {
            chdir(getenv("HOME"));  // cd 或 cd ~
        } else if (strcmp(args[1], "-") == 0) {
            if (strlen(prev_dir) > 0) {
                printf("%s\n", prev_dir);
                chdir(prev_dir);   // cd -
            } else {
                fprintf(stderr, "cd: OLDPWD not set\n");
            }
        } else {
            char *path = args[1];
            if (path[0] == '~') {
                char fullpath[1024];
                snprintf(fullpath, sizeof(fullpath), "%s%s", getenv("HOME"), path + 1);  // 展开 ~/
                chdir(fullpath);
            } else {
                chdir(path);
            }
        }

        strncpy(prev_dir, cwd, sizeof(prev_dir));  // 保存当前目录用于 cd -
    } else if (strcmp(args[0], "exit") == 0) {
        exit(0);  // 退出 shell
    } else if (strcmp(args[0], "export") == 0) {
        char *var = strtok(args[1], "=");
        char *val = strtok(NULL, "");
        if (var && val) setenv(var, val, 1);  // 设置环境变量
    }
}

// 执行命令：支持外部命令、后台、管道、重定向
void exec_command(char **args, int background, char *infile, char *outfile, int pipe_index) {
    int pipefd[2];
    if (pipe_index > 0) {
        pipe(pipefd);  // 创建管道
    }

    pid_t pid = fork();
    if (pid == 0) {
        // 子进程
        if (infile) {
            int fd = open(infile, O_RDONLY);
            dup2(fd, STDIN_FILENO); close(fd);  // 输入重定向
        }
        if (outfile) {
            int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            dup2(fd, STDOUT_FILENO); close(fd);  // 输出重定向
        }
        if (pipe_index > 0) {
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[0]); close(pipefd[1]);  // 写端重定向输出
        }
        if (!is_builtin(args[0])) {
            execvp(args[0], args);
            perror("execvp");
            exit(1);
        } else {
            exec_builtin(args);
            exit(0);
        }
    } else {
        // 父进程
        if (!background)
            waitpid(pid, NULL, 0);  // 等待子进程（前台）
        if (pipe_index > 0) {
            close(pipefd[1]);
            pid_t pid2 = fork();
            if (pid2 == 0) {
                dup2(pipefd[0], STDIN_FILENO);  // 接收管道输入
                close(pipefd[0]);
                execvp(args[pipe_index], &args[pipe_index]);
                perror("execvp");
                exit(1);
            } else {
                close(pipefd[0]);
                if (!background) waitpid(pid2, NULL, 0);
            }
        }
    }
}

int main() {
    signal(SIGCHLD, sigchld_handler);  // 安装信号处理器

    while (1) {
        print_prompt();  // 显示提示符

        char line[MAX_LINE];
        if (!fgets(line, sizeof(line), stdin)) break;

        char *args[MAX_ARGS];
        int background;
        char *infile, *outfile;
        int pipe_index;

        parse_command(line, args, &background, &infile, &outfile, &pipe_index);
        if (args[0] == NULL) continue;

        if (is_builtin(args[0])) {
            exec_builtin(args);  // 内建命令
        } else {
            exec_command(args, background, infile, outfile, pipe_index);  // 外部命令
        }
    }

    return 0;
}
