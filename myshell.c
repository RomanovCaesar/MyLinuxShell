#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_ARGS 100
#define MAX_LINE 1024

char prev_dir[1024] = "";

void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void print_prompt() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s$ ", cwd);
    } else {
        perror("getcwd");
    }
    fflush(stdout);
}

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

int is_builtin(char *cmd) {
    return strcmp(cmd, "cd") == 0 || strcmp(cmd, "exit") == 0 || strcmp(cmd, "export") == 0;
}

void exec_builtin(char **args) {
    if (strcmp(args[0], "cd") == 0) {
        char cwd[1024];
        getcwd(cwd, sizeof(cwd));

        if (args[1] == NULL || strcmp(args[1], "~") == 0) {
            chdir(getenv("HOME"));
        } else if (strcmp(args[1], "-") == 0) {
            if (strlen(prev_dir) > 0) {
                printf("%s\n", prev_dir);
                chdir(prev_dir);
            } else {
                fprintf(stderr, "cd: OLDPWD not set\n");
            }
        } else {
            char *path = args[1];
            if (path[0] == '~') {
                char fullpath[1024];
                snprintf(fullpath, sizeof(fullpath), "%s%s", getenv("HOME"), path + 1);
                chdir(fullpath);
            } else {
                chdir(path);
            }
        }
        strncpy(prev_dir, cwd, sizeof(prev_dir));
    } else if (strcmp(args[0], "exit") == 0) {
        exit(0);
    } else if (strcmp(args[0], "export") == 0) {
        char *var = strtok(args[1], "=");
        char *val = strtok(NULL, "");
        if (var && val) setenv(var, val, 1);
    }
}

void exec_command(char **args, int background, char *infile, char *outfile, int pipe_index) {
    int pipefd[2];
    if (pipe_index > 0) {
        pipe(pipefd);
    }

    pid_t pid = fork();
    if (pid == 0) {
        if (infile) {
            int fd = open(infile, O_RDONLY);
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        if (outfile) {
            int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        if (pipe_index > 0) {
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[0]);
            close(pipefd[1]);
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
        if (!background) waitpid(pid, NULL, 0);
        if (pipe_index > 0) {
            close(pipefd[1]);
            pid_t pid2 = fork();
            if (pid2 == 0) {
                dup2(pipefd[0], STDIN_FILENO);
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
    signal(SIGCHLD, sigchld_handler);

    while (1) {
        print_prompt();

        char line[MAX_LINE];
        if (!fgets(line, sizeof(line), stdin)) break;

        char *args[MAX_ARGS];
        int background;
        char *infile, *outfile;
        int pipe_index;

        parse_command(line, args, &background, &infile, &outfile, &pipe_index);
        if (args[0] == NULL) continue;

        if (is_builtin(args[0])) {
            exec_builtin(args);
        } else {
            exec_command(args, background, infile, outfile, pipe_index);
        }
    }

    return 0;
}
