/* Lists nearby Wi-Fi networks through nl80211, without iw or libnl.
 *
 * The bench rootfs has no iw, and iw plus libnl would be another large exec
 * on noMMU. This brings the interface up, asks the driver to scan, waits for
 * the scan to finish and prints one line per BSS: BSSID, frequency, signal
 * and SSID. Usage: wifi-scan [ifname]   (default wlan0) */
#include <errno.h>
#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <linux/nl80211.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUF 16384

static int nl;
static uint32_t seq = 1;
static char buf[BUF];

struct msg {
    struct nlmsghdr h;
    struct genlmsghdr g;
    char attrs[256];
};

static void put_attr(struct msg *m, uint16_t type, const void *data, uint16_t len)
{
    struct nlattr *a = (struct nlattr *)((char *)m + NLMSG_ALIGN(m->h.nlmsg_len));
    a->nla_type = type;
    a->nla_len = (uint16_t)(NLA_HDRLEN + len);
    memcpy((char *)a + NLA_HDRLEN, data, len);
    m->h.nlmsg_len = NLMSG_ALIGN(m->h.nlmsg_len) + NLA_ALIGN(a->nla_len);
}

static void start(struct msg *m, uint16_t family, uint8_t cmd, uint16_t flags)
{
    memset(m, 0, sizeof *m);
    m->h.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
    m->h.nlmsg_type = family;
    m->h.nlmsg_flags = (uint16_t)(NLM_F_REQUEST | flags);
    m->h.nlmsg_seq = seq++;
    m->g.cmd = cmd;
    m->g.version = 1;
}

static int send_msg(struct msg *m)
{
    return send(nl, m, m->h.nlmsg_len, 0) < 0 ? -1 : 0;
}

/* Walks the attributes after a genetlink header. */
#define FOR_ATTR(a, start, len) \
    for (struct nlattr *a = (struct nlattr *)(start); \
         (char *)a + NLA_HDRLEN <= (char *)(start) + (len) && a->nla_len >= NLA_HDRLEN && \
         (char *)a + a->nla_len <= (char *)(start) + (len); \
         a = (struct nlattr *)((char *)a + NLA_ALIGN(a->nla_len)))
#define ADATA(a) ((char *)(a) + NLA_HDRLEN)
#define ALEN(a) ((int)(a)->nla_len - NLA_HDRLEN)
#define ATYPE(a) ((a)->nla_type & NLA_TYPE_MASK)

/* Receives until the reply to this request ends. handler sees each message;
 * returns 0, or a negative errno from an NLMSG_ERROR. */
static int receive(void (*handler)(struct nlmsghdr *, void *), void *ctx, int dump)
{
    for (;;) {
        ssize_t len = recv(nl, buf, sizeof buf, 0);
        if (len < 0) { if (errno == EINTR) continue; return -errno; }
        for (struct nlmsghdr *h = (struct nlmsghdr *)buf; NLMSG_OK(h, (unsigned)len);
             h = NLMSG_NEXT(h, len)) {
            if (h->nlmsg_type == NLMSG_DONE) return 0;
            if (h->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr *e = NLMSG_DATA(h);
                return e->error;
            }
            if (handler) handler(h, ctx);
            if (!dump) return 0;
        }
    }
}

struct family { uint16_t id; uint32_t scan_group; };

static void on_family(struct nlmsghdr *h, void *ctx)
{
    struct family *f = ctx;
    char *p = (char *)NLMSG_DATA(h) + GENL_HDRLEN;
    int len = (int)h->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);
    FOR_ATTR(a, p, len) {
        if (ATYPE(a) == CTRL_ATTR_FAMILY_ID) f->id = *(uint16_t *)ADATA(a);
        if (ATYPE(a) != CTRL_ATTR_MCAST_GROUPS) continue;
        FOR_ATTR(g, ADATA(a), ALEN(a)) {
            const char *name = NULL;
            uint32_t id = 0;
            FOR_ATTR(x, ADATA(g), ALEN(g)) {
                if (ATYPE(x) == CTRL_ATTR_MCAST_GRP_NAME) name = ADATA(x);
                if (ATYPE(x) == CTRL_ATTR_MCAST_GRP_ID) id = *(uint32_t *)ADATA(x);
            }
            if (name && !strcmp(name, "scan")) f->scan_group = id;
        }
    }
}

static void print_ssid(const unsigned char *ie, int len)
{
    while (len >= 2 && ie[1] + 2 <= len) {
        if (ie[0] == 0) {
            if (!ie[1]) { printf("(hidden)"); return; }
            for (int i = 0; i < ie[1]; i++)
                putchar(ie[2 + i] >= 32 && ie[2 + i] < 127 ? ie[2 + i] : '?');
            return;
        }
        len -= ie[1] + 2;
        ie += ie[1] + 2;
    }
}

static void on_bss(struct nlmsghdr *h, void *ctx)
{
    int *count = ctx;
    char *p = (char *)NLMSG_DATA(h) + GENL_HDRLEN;
    int len = (int)h->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);
    FOR_ATTR(a, p, len) {
        if (ATYPE(a) != NL80211_ATTR_BSS) continue;
        const unsigned char *mac = NULL, *ie = NULL;
        int ielen = 0, mbm = 0;
        uint32_t freq = 0, status = 0;
        int has_status = 0;
        FOR_ATTR(b, ADATA(a), ALEN(a)) {
            switch (ATYPE(b)) {
            case NL80211_BSS_BSSID: mac = (unsigned char *)ADATA(b); break;
            case NL80211_BSS_FREQUENCY: freq = *(uint32_t *)ADATA(b); break;
            case NL80211_BSS_SIGNAL_MBM: mbm = *(int32_t *)ADATA(b); break;
            case NL80211_BSS_INFORMATION_ELEMENTS:
                ie = (unsigned char *)ADATA(b); ielen = ALEN(b); break;
            case NL80211_BSS_STATUS: status = *(uint32_t *)ADATA(b); has_status = 1; break;
            }
        }
        if (!mac) continue;
        printf("%02x:%02x:%02x:%02x:%02x:%02x %5u MHz %4d dBm%s  ", mac[0], mac[1], mac[2],
               mac[3], mac[4], mac[5], freq, mbm / 100,
               has_status && status == NL80211_BSS_STATUS_ASSOCIATED ? " *" : "  ");
        if (ie) print_ssid(ie, ielen);
        putchar('\n');
        ++*count;
    }
}

static void on_event(struct nlmsghdr *h, void *ctx)
{
    struct genlmsghdr *g = NLMSG_DATA(h);
    if (g->cmd == NL80211_CMD_NEW_SCAN_RESULTS || g->cmd == NL80211_CMD_SCAN_ABORTED)
        *(int *)ctx = g->cmd;
}

static int set_up(const char *name)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    int r = ioctl(s, SIOCGIFFLAGS, &ifr);
    if (!r && !(ifr.ifr_flags & IFF_UP)) {
        ifr.ifr_flags |= IFF_UP;
        r = ioctl(s, SIOCSIFFLAGS, &ifr);
    }
    close(s);
    return r;
}

int main(int argc, char **argv)
{
    const char *ifname = argc > 1 ? argv[1] : "wlan0";
    uint32_t ifindex = if_nametoindex(ifname);
    if (!ifindex) { fprintf(stderr, "wifi-scan: no interface %s\n", ifname); return 1; }
    if (set_up(ifname)) { perror("wifi-scan: bring up"); return 1; }

    nl = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
    struct sockaddr_nl sa = {.nl_family = AF_NETLINK};
    if (nl < 0 || bind(nl, (struct sockaddr *)&sa, sizeof sa)) { perror("wifi-scan: netlink"); return 1; }
    struct timeval tv = {15, 0};
    setsockopt(nl, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    struct msg m;
    struct family fam = {0, 0};
    start(&m, GENL_ID_CTRL, CTRL_CMD_GETFAMILY, 0);
    put_attr(&m, CTRL_ATTR_FAMILY_NAME, "nl80211", 8);
    if (send_msg(&m) || receive(on_family, &fam, 0) || !fam.id) {
        fprintf(stderr, "wifi-scan: nl80211 not available (no cfg80211?)\n");
        return 1;
    }
    if (fam.scan_group &&
        setsockopt(nl, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &fam.scan_group, sizeof fam.scan_group))
        fam.scan_group = 0;

    start(&m, fam.id, NL80211_CMD_TRIGGER_SCAN, NLM_F_ACK);
    put_attr(&m, NL80211_ATTR_IFINDEX, &ifindex, 4);
    int r = send_msg(&m) ? -errno : receive(NULL, NULL, 1);
    if (r && r != -EBUSY) { fprintf(stderr, "wifi-scan: trigger: %s\n", strerror(-r)); return 1; }

    /* Wait for the scan group event; fall back to a fixed wait without it. */
    int done = 0;
    if (fam.scan_group) {
        for (int i = 0; i < 20 && !done; i++)
            if (receive(on_event, &done, 0) == -EAGAIN) break;
    } else {
        sleep(6);
    }
    if (done == NL80211_CMD_SCAN_ABORTED) fprintf(stderr, "wifi-scan: scan aborted\n");

    start(&m, fam.id, NL80211_CMD_GET_SCAN, NLM_F_DUMP);
    put_attr(&m, NL80211_ATTR_IFINDEX, &ifindex, 4);
    int count = 0;
    if (send_msg(&m) || (r = receive(on_bss, &count, 1))) {
        fprintf(stderr, "wifi-scan: dump: %s\n", strerror(-r));
        return 1;
    }
    printf("%d networks\n", count);
    return 0;
}
