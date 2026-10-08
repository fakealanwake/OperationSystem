#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

/* переворачивает строку и пишет её в stdout вместе с '\n' */
void write_rev(char *s, int n) {
    // abcd
    //      i   j 
    // d    c   b   a
    // [0] [1] [2] [3] [4]
    //          
    for (int i = 0; i < n / 2; i++) {
        char t = s[i];
        s[i] = s[n - 1 - i];
        s[n - 1 - i] = t;
    }
    s[n] = '\n';
    if (write(STDOUT_FILENO, s, n + 1) != n + 1) {
        perror("write");
        _exit(1);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2)
        return 1;
    /* файл результата, дальше пишем в него как в stdout */
    int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open");
        return 1;
    }
    if (dup2(fd, STDOUT_FILENO) == -1) {
        perror("dup2");
        return 1;
    }
    close(fd);
    /* собираем строку до '\n' и переворачиваем */
    char buf[256];
    int n = 0;
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == '\n') {
            write_rev(buf, n);
            n = 0;
        } else if (n < 255) {
            buf[n++] = c;
        }
    }
    if (n > 0)
        write_rev(buf, n);
    return 0;
}
