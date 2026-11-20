/* Configures Android USB tethering on CN15 whenever the phone (re)appears.
 *
 * rndis_host or cdc_ncm creates usbN each time the phone enumerates: at boot,
 * after tethering is toggled, after adbd restarts. This daemon waits for
 * RTM_NEWLINK on rtnetlink, brings a new usbN up, runs udhcpc once and
 * announces the address on /dev/kmsg, which reaches the LCD and the serial
 * console. It sleeps in recv() otherwise: no polling, and no exec except the
 * one udhcpc per new link. That matters on noMMU, where every exec needs a
 * contiguous block for the whole binary. It also runs DHCP on wlanN each time
 * wpa_supplicant associates (carrier up), and prints the SSH address.
 * udhcpc calls this binary back as its event script (see lease_event).
 * Usage: usb-tether [frame-port] */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <net/route.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

/* udhcpc runs this same binary as its event script. Busybox's default.script
 * would exec sh, ifconfig and route while udhcpc is still running: three more
 * 245 KB contiguous images, more than the board has once wpa_supplicant runs. */
#define SCRIPT "/usr/bin/usb-tether"
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

static int set_inet(int sock, const char *name, unsigned long req, const char *text)
{
    struct ifreq ifr;
    struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    sin->sin_family = AF_INET;
    if (inet_pton(AF_INET, text, &sin->sin_addr) != 1) return -1;
    return ioctl(sock, req, &ifr);
}

/* udhcpc event script: interface, ip, subnet, router and dns arrive in the
 * environment. Applies the lease with ioctls instead of ifconfig/route. */
static int lease_event(const char *event)
{
    const char *name = getenv("interface"), *ip = getenv("ip");
    const char *mask = getenv("subnet"), *router = getenv("router"), *dns = getenv("dns");
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (!name || sock < 0) return 1;
    if (!strcmp(event, "deconfig")) {
        set_inet(sock, name, SIOCSIFADDR, "0.0.0.0");
        return set_up(sock, name) ? 1 : 0;
    }
    if (strcmp(event, "bound") && strcmp(event, "renew")) return 0;
    if (!ip || set_inet(sock, name, SIOCSIFADDR, ip)) return 1;
    if (mask) set_inet(sock, name, SIOCSIFNETMASK, mask);
    if (router) {
        char gw[INET_ADDRSTRLEN] = {0};
        strncpy(gw, router, sizeof gw - 1);
        gw[strcspn(gw, " ")] = 0;
        struct rtentry rt;
        memset(&rt, 0, sizeof rt);
        struct sockaddr_in *g = (struct sockaddr_in *)&rt.rt_gateway;
        ((struct sockaddr_in *)&rt.rt_dst)->sin_family = AF_INET;
        ((struct sockaddr_in *)&rt.rt_genmask)->sin_family = AF_INET;
        g->sin_family = AF_INET;
        rt.rt_flags = RTF_UP | RTF_GATEWAY;
        rt.rt_dev = (char *)name;
        /* EEXIST: another link already owns the default route; keep it. */
        if (inet_pton(AF_INET, gw, &g->sin_addr) == 1) ioctl(sock, SIOCADDRT, &rt);
    }
    if (dns) {
        FILE *f = fopen("/etc/resolv.conf", "w");
        if (f) {
            char list[256];
            strncpy(list, dns, sizeof list - 1);
            list[sizeof list - 1] = 0;
            for (char *s = strtok(list, " "); s; s = strtok(NULL, " "))
                fprintf(f, "nameserver %s\n", s);
            fclose(f);
        }
    }
    return 0;
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

static int is_tether(const char *name) { return !strncmp(name, "usb", 3); }
static int is_wifi(const char *name) { return !strncmp(name, "wlan", 4); }

static void configure(int sock, const char *name)
{
    char ip[INET_ADDRSTRLEN];
    for (int i = 0; i < ATTEMPTS; i++) {
        if (set_up(sock, name) == 0 && dhcp(name) == 0 && address(sock, name, ip, sizeof ip) == 0) {
            if (is_wifi(name))
                announce("%s up at %s, ssh root@%s\n", name, ip, ip);
            else
                announce("%s up at %s, gateway frame URL http://%s:%s/frame\n",
                         name, ip, ip, frame_port);
            return;
        }
        /* udhcpc is a 245 KB exec; freeing the page cache helps free pages
         * merge back into a block that size. */
        int fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
        if (fd >= 0) { if (write(fd, "3", 1) < 0) {} close(fd); }
        sleep(3);
    }
    announce("%s: no DHCP lease%s\n", name,
             is_wifi(name) ? "" : "; is USB tethering on?");
}

/* wlanN exists from boot; it only needs DHCP once wpa_supplicant associates,
 * which the kernel reports as IFF_RUNNING (carrier) on RTM_NEWLINK. */
static int wifi_configured;

static void wifi_link(int sock, const char *name, unsigned flags)
{
    if (!(flags & IFF_RUNNING)) { wifi_configured = 0; return; }
    if (wifi_configured) return;
    wifi_configured = 1;
    configure(sock, name);
}

static unsigned if_flags(int sock, const char *name)
{
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    return ioctl(sock, SIOCGIFFLAGS, &ifr) ? 0 : (unsigned)ifr.ifr_flags;
}

int main(int argc, char **argv)
{
    /* Invoked by udhcpc as its script: argv[1] is the event name. */
    if (argc > 1 && getenv("interface") && !strchr("0123456789", argv[1][0]))
        return lease_event(argv[1]);
    if (argc > 1) frame_port = argv[1];
    int ctl = socket(AF_INET, SOCK_DGRAM, 0);
    int nl = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    struct sockaddr_nl sa = {.nl_family = AF_NETLINK, .nl_groups = RTMGRP_LINK};
    if (ctl < 0 || nl < 0 || bind(nl, (struct sockaddr *)&sa, sizeof sa)) {
        perror("usb-tether"); return 1;
    }
    /* A phone already tethered at boot appeared before we subscribed. */
    struct if_nameindex *all = if_nameindex();
    for (struct if_nameindex *i = all; i && i->if_index; i++) {
        if (is_tether(i->if_name) && !handled_index) {
            handled_index = (int)i->if_index;
            configure(ctl, i->if_name);
        } else if (is_wifi(i->if_name)) {
            wifi_link(ctl, i->if_name, if_flags(ctl, i->if_name));
        }
    }
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
                if (is_tether(name) && ifi->ifi_index != handled_index) {
                    handled_index = ifi->ifi_index;
                    configure(ctl, name);
                } else if (is_wifi(name)) {
                    wifi_link(ctl, name, ifi->ifi_flags);
                }
                break;
            }
        }
    }
}
