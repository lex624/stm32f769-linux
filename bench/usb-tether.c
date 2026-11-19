/* Configures Android USB tethering on CN15 whenever the phone (re)appears.
 *
 * rndis_host or cdc_ncm creates usbN each time the phone enumerates: at boot,
 * after tethering is toggled, after adbd restarts. This daemon waits for
 * RTM_NEWLINK on rtnetlink, brings a new usbN up, runs udhcpc once and
 * announces the address on /dev/kmsg, which reaches the LCD and the serial
 * console. It sleeps in recv() otherwise: no polling, and no exec except the
 * one udhcpc per new link. That matters on noMMU, where every exec needs a
 * contiguous block for the whole binary. Usage: usb-tether [frame-port] */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define SCRIPT "/usr/share/udhcpc/default.script"
#define ATTEMPTS 3

static int handled_index;
static const char *frame_port = "8090";

__attribute__((format(printf, 1, 2)))
static void announce(const char *fmt, ...)
{
    char line[200];
    va_list ap;
    int n = snprintf(line, sizeof line, "<5>usb-tether: ");
    va_start(ap, fmt);
    n += vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
    va_end(ap);
    if (n >= (int)sizeof line) n = sizeof line - 1;
    int fd = open("/dev/kmsg", O_WRONLY);
    if (fd >= 0) {
        if (write(fd, line, (size_t)n) < 0) {}
        close(fd);
    }
}

static int set_up(int sock, const char *name)
{
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFFLAGS, &ifr)) return -1;
    if (ifr.ifr_flags & IFF_UP) return 0;
    ifr.ifr_flags |= IFF_UP;
    return ioctl(sock, SIOCSIFFLAGS, &ifr);
}

static int address(int sock, const char *name, char *out, size_t len)
{
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFADDR, &ifr)) return -1;
    return inet_ntop(AF_INET, &((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr, out,
                     (socklen_t)len) ? 0 : -1;
}

static int dhcp(const char *name)
{
    pid_t pid = vfork();
    if (pid == 0) {
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, 0); dup2(null, 1); dup2(null, 2); }
        execl("/sbin/udhcpc", "udhcpc", "-f", "-n", "-q", "-t", "6", "-T", "2",
              "-s", SCRIPT, "-i", name, (char *)0);
        _exit(127);
    }
    if (pid < 0) return -1;
    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static void configure(int sock, int index, const char *name)
{
    char ip[INET_ADDRSTRLEN];
    handled_index = index;
    for (int i = 0; i < ATTEMPTS; i++) {
        if (set_up(sock, name) == 0 && dhcp(name) == 0 && address(sock, name, ip, sizeof ip) == 0) {
            announce("%s up at %s, gateway frame URL http://%s:%s/frame\n",
                     name, ip, ip, frame_port);
            return;
        }
        sleep(3);
    }
    announce("%s: no DHCP lease; is USB tethering on?\n", name);
}

static int is_tether(const char *name) { return !strncmp(name, "usb", 3); }

int main(int argc, char **argv)
{
    if (argc > 1) frame_port = argv[1];
    int ctl = socket(AF_INET, SOCK_DGRAM, 0);
    int nl = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    struct sockaddr_nl sa = {.nl_family = AF_NETLINK, .nl_groups = RTMGRP_LINK};
    if (ctl < 0 || nl < 0 || bind(nl, (struct sockaddr *)&sa, sizeof sa)) {
        perror("usb-tether"); return 1;
    }
    /* A phone already tethered at boot appeared before we subscribed. */
    struct if_nameindex *all = if_nameindex();
    for (struct if_nameindex *i = all; i && i->if_index; i++)
        if (is_tether(i->if_name)) { configure(ctl, (int)i->if_index, i->if_name); break; }
    if (all) if_freenameindex(all);

    static char buf[8192];
    for (;;) {
        ssize_t len = recv(nl, buf, sizeof buf, 0);
        if (len < 0) { if (errno == EINTR || errno == ENOBUFS) continue; perror("recv"); sleep(1); continue; }
        for (struct nlmsghdr *h = (struct nlmsghdr *)buf; NLMSG_OK(h, (unsigned)len);
             h = NLMSG_NEXT(h, len)) {
            if (h->nlmsg_type != RTM_NEWLINK && h->nlmsg_type != RTM_DELLINK) continue;
            struct ifinfomsg *ifi = NLMSG_DATA(h);
            if (h->nlmsg_type == RTM_DELLINK) {
                if (ifi->ifi_index == handled_index) handled_index = 0;
                continue;
            }
            int alen = (int)IFLA_PAYLOAD(h);
            for (struct rtattr *a = IFLA_RTA(ifi); RTA_OK(a, alen); a = RTA_NEXT(a, alen)) {
                if (a->rta_type != IFLA_IFNAME) continue;
                const char *name = RTA_DATA(a);
                if (is_tether(name) && ifi->ifi_index != handled_index)
                    configure(ctl, ifi->ifi_index, name);
                break;
            }
        }
    }
}
