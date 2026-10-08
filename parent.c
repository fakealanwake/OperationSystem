#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

int read_line(char *buf, int max) {
    int n = 0;
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != '\n') {
        if (n < max - 1)
            buf[n++] = c;
    }
    buf[n] = '\0';
    return n;
}

int main(void) {
    char file1[256], file2[256];

    if (read_line(file1, 256) <= 0 || read_line(file2, 256) <= 0)
        return 1;

    int p1[2], p2[2];

    if (pipe(p1) == -1 || pipe(p2) == -1) {
        perror("pipe");
        return 1;
    }

    pid_t pid1 = fork();
    if (pid1 == -1) {
        perror("fork");
        return 1;
    }
    if (pid1 == 0) {

        if (dup2(p1[0], STDIN_FILENO) == -1) {
            perror("dup2");
            return 1;
        }
        close(p1[0]);
        close(p1[1]);
        close(p2[0]);
        close(p2[1]);
        execl("./child", "child", file1, (char *)NULL);
        perror("execl");
        return 1;
    }

    pid_t pid2 = fork();
    if (pid2 == -1) {
        perror("fork");
        return 1;
    }
    if (pid2 == 0) {

        if (dup2(p2[0], STDIN_FILENO) == -1) {
            perror("dup2");
            return 1;
        }
        close(p1[0]);
        close(p1[1]);
        close(p2[0]);
        close(p2[1]);
        execl("./child", "child", file2, (char *)NULL);
        perror("execl");
        return 1;
    }


    close(p1[0]);
    close(p2[0]);


    srand((unsigned)time(NULL));
    char c;
    int at_start = 1;
    int to_first = 1;
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (at_start) {
            to_first = (rand() % 100) < 80;
            at_start = 0;
        }
        int out = to_first ? p1[1] : p2[1];
        if (write(out, &c, 1) != 1) {
            perror("write");
            return 1;
        }
        if (c == '\n')
            at_start = 1;
    }


    close(p1[1]);
    close(p2[1]);

    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
    return 0;
}
