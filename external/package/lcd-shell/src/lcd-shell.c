/* Mirror a single interactive Linux PTY to the Discovery LCD and the ST-LINK
 * serial console. The shell owns the PTY so line editing, echo and signals
 * are handled once by Linux. UART remains the keyboard on the bench. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static void shell(void)
{
    execl("/bin/sh", "-sh", "-i", (char *)0);
    _exit(127);
}

static int copy_bytes(int fd, const char *p, size_t n)
{
    while (n) {
        ssize_t count = write(fd, p, n);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return -1;
        p += count;
        n -= (size_t)count;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2) {
        /* Runs after exec, with an independent address space on noMMU. */
        if (setsid() < 0) return 1;
        int tty = open(argv[1], O_RDWR);
        if (tty < 0 || ioctl(tty, TIOCSCTTY, 0) < 0) return 1;
        for (int fd = 0; fd <= 2; ++fd)
            if (dup2(tty, fd) < 0) return 1;
        if (tty > 2) close(tty);
        shell();
    }
    int lcd = open("/dev/tty1", O_WRONLY | O_NOCTTY | O_CLOEXEC);
    if (lcd < 0 || access("/dev/fb0", F_OK) < 0) shell();
    if (mount("devpts", "/dev/pts", "devpts", 0, NULL) < 0 && errno != EBUSY) shell();
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (master < 0 || grantpt(master) < 0 || unlockpt(master) < 0) shell();
    char *slave = ptsname(master);
    struct termios saved, raw;
    if (!slave || tcgetattr(0, &saved) < 0) shell();
    struct winsize size;
    if (ioctl(lcd, TIOCGWINSZ, &size) == 0) ioctl(master, TIOCSWINSZ, &size);
    raw = saved;
    cfmakeraw(&raw);
    if (tcsetattr(0, TCSANOW, &raw) < 0) shell();
    char *child_args[] = {argv[0], slave, NULL};
    pid_t child = vfork();
    if (child == 0) {
        execv("/usr/bin/lcd-shell", child_args);
        _exit(127);
    }
    if (child < 0) {
        tcsetattr(0, TCSANOW, &saved);
        shell();
    }
    struct pollfd ports[] = {{0, POLLIN, 0}, {master, POLLIN, 0}};
    char buf[256];
    for (;;) {
        int ready = poll(ports, 2, -1);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) break;
        if (ports[1].revents) {
            ssize_t n = read(master, buf, sizeof(buf));
            if (n <= 0 || copy_bytes(1, buf, n) < 0) break;
            if (lcd >= 0 && copy_bytes(lcd, buf, n) < 0) { close(lcd); lcd = -1; }
        }
        if (ports[0].revents) {
            ssize_t n = read(0, buf, sizeof(buf));
            if (n <= 0 || copy_bytes(master, buf, n) < 0) break;
        }
    }
    close(master);
    tcsetattr(0, TCSANOW, &saved);
    waitpid(child, NULL, 0);
    return 0;
}
