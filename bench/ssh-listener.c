/* One interactive session at a time fits the F769 SRAM budget. BusyBox init
 * respawns this listener after Dropbear exits. No fork or duplicate SSH image.
 * Listens on every interface (bench Ethernet, USB tethering, Wi-Fi); Dropbear
 * is built without password auth, so only authorized_keys holders get in. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
int main(void)
{
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(22),
                               .sin_addr.s_addr = htonl(INADDR_ANY)};
    int yes = 1, client;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) ||
        bind(fd, (struct sockaddr *)&addr, sizeof addr) || listen(fd, 1)) {
        perror("bench SSH listen"); sleep(3); return 1;
    }
    do { client = accept(fd, NULL, NULL); } while (client < 0 && errno == EINTR);
    if (client < 0) return 1;
    close(fd);
    if (dup2(client, STDIN_FILENO) < 0 || dup2(client, STDOUT_FILENO) < 0) return 1;
    if (client > 1) close(client);
    /* Dropbear needs one free contiguous ~244 KB block. Just after another
     * exec exits, its pages may not have merged back yet: drop the page cache
     * and retry for a few seconds before giving up on this connection. */
    for (int i = 0; i < 8; i++) {
        execl("/usr/sbin/dropbear", "dropbear", "-i", "-s", "-r",
              "/etc/dropbear/dropbear_ed25519_host_key", (char *)0);
        if (errno != ENOMEM) break;
        int fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
        if (fd >= 0) { if (write(fd, "3", 1) < 0) {} close(fd); }
        sleep(1);
    }
    perror("dropbear"); return 1;
}
