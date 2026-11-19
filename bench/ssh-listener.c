/* One interactive session at a time fits the F769 SRAM budget. BusyBox init
 * respawns this listener after Dropbear exits. No fork or duplicate SSH image. */
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
int main(void)
{
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(22)};
    int yes = 1, client;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    inet_pton(AF_INET, "10.77.77.2", &addr.sin_addr);
    if (fd < 0 || setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) ||
        bind(fd, (struct sockaddr *)&addr, sizeof addr) || listen(fd, 1)) {
        perror("bench SSH listen"); sleep(3); return 1;
    }
    do { client = accept(fd, NULL, NULL); } while (client < 0 && errno == EINTR);
    if (client < 0) return 1;
    close(fd);
    if (dup2(client, STDIN_FILENO) < 0 || dup2(client, STDOUT_FILENO) < 0) return 1;
    if (client > 1) close(client);
    execl("/usr/sbin/dropbear", "dropbear", "-i", "-s", "-r",
          "/etc/dropbear/dropbear_ed25519_host_key", (char *)0);
    perror("dropbear"); return 1;
}
