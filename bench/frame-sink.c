/* Receives JPEG frames and control samples from the Android gateway, over
 * USB tethering (CN15) or Wi-Fi.
 *
 *   POST /frame      body is one image/jpeg frame, stored as /tmp/frame.jpg
 *   GET  /frame.jpg  returns the latest stored frame
 *   POST /control    body is the 6-byte control frame also used over BLE:
 *                    version 1, sequence, int16 LE throttle and steering
 *                    in thousandths (each within -1000..1000)
 *   GET  /control    text: count, last sequence, throttle, steering, age
 *   OPTIONS *        CORS and Private Network Access preflight for Chrome
 *
 * One connection at a time, no fork: the F769 has about 3 MiB free. The body
 * is streamed to a temporary file in /tmp (tmpfs) and renamed into place, so
 * a reader never sees a partial frame. Usage: frame-sink [port] [bind-ip] */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define MAX_FRAME (192 * 1024)
#define FRAME "/tmp/frame.jpg"
#define FRAME_TMP "/tmp/frame.jpg.part"

static const char cors[] =
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type\r\n"
    "Access-Control-Allow-Private-Network: true\r\n"
    "Access-Control-Max-Age: 600\r\n"
    "Connection: close\r\n";

static unsigned long frames;

/* Last accepted control sample. This bench only reports it; anything driving
 * actuators must also stop when samples go stale (see "age" in GET /control). */
static struct {
    unsigned long count;
    unsigned sequence;
    int throttle, steering;
    struct timeval at;
} control;

static int send_all(int fd, const void *buf, size_t len)
{
    const char *p = buf;
    while (len) {
        ssize_t n = write(fd, p, len);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        p += n; len -= (size_t)n;
    }
    return 0;
}

static void reply(int fd, const char *status, const char *extra)
{
    char head[512];
    int n = snprintf(head, sizeof head, "HTTP/1.1 %s\r\n%s%sContent-Length: 0\r\n\r\n",
                     status, cors, extra ? extra : "");
    if (n > 0) send_all(fd, head, (size_t)n);
}

/* Reads until the blank line ending the headers. Returns header length and
 * leaves any body bytes already read in buf[hlen..*got). */
static int read_headers(int fd, char *buf, size_t cap, size_t *got)
{
    *got = 0;
    while (*got < cap - 1) {
        ssize_t n = read(fd, buf + *got, cap - 1 - *got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        *got += (size_t)n;
        buf[*got] = 0;
        char *end = strstr(buf, "\r\n\r\n");
        if (end) return (int)(end - buf) + 4;
    }
    return -1;
}

static long content_length(const char *h)
{
    for (const char *p = h; (p = strchr(p, '\n')); ) {
        p++;
        if (!strncasecmp(p, "Content-Length:", 15)) return strtol(p + 15, NULL, 10);
    }
    return -1;
}

static void store_frame(int fd, char *buf, size_t cap, size_t have, long len)
{
    if (len <= 0 || len > MAX_FRAME) { reply(fd, "413 Payload Too Large", NULL); return; }
    int out = open(FRAME_TMP, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { reply(fd, "500 Internal Server Error", NULL); return; }
    long left = len;
    int ok = 1;
    if (have) {
        size_t take = have > (size_t)left ? (size_t)left : have;
        ok = send_all(out, buf, take) == 0;
        left -= (long)take;
    }
    while (ok && left > 0) {
        ssize_t n = read(fd, buf, (size_t)left < cap ? (size_t)left : cap);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = 0; break; }
        ok = send_all(out, buf, (size_t)n) == 0;
        left -= n;
    }
    if (close(out) || !ok || rename(FRAME_TMP, FRAME)) {
        unlink(FRAME_TMP);
        reply(fd, "400 Bad Request", NULL);
        return;
    }
    char extra[64];
    snprintf(extra, sizeof extra, "X-Frame-Seq: %lu\r\n", ++frames);
    reply(fd, "204 No Content", extra);
    if (frames == 1 || frames % 25 == 0)
        printf("frame-sink: %lu frames, last %ld bytes\n", frames, len), fflush(stdout);
}

static void kmsg(const char *line)
{
    int k = open("/dev/kmsg", O_WRONLY);
    if (k >= 0) { if (write(k, line, strlen(line)) < 0) {} close(k); }
}

static void store_control(int fd, char *buf, size_t have, long len)
{
    unsigned char *f = (unsigned char *)buf;
    while (len == 6 && have < 6) {
        ssize_t n = read(fd, buf + have, 6 - have);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        have += (size_t)n;
    }
    if (len != 6 || have < 6 || f[0] != 1) { reply(fd, "400 Bad Request", NULL); return; }
    int throttle = (short)(f[2] | f[3] << 8), steering = (short)(f[4] | f[5] << 8);
    if (throttle < -1000 || throttle > 1000 || steering < -1000 || steering > 1000) {
        reply(fd, "400 Bad Request", NULL);
        return;
    }
    control.count++;
    control.sequence = f[1];
    control.throttle = throttle;
    control.steering = steering;
    gettimeofday(&control.at, NULL);
    reply(fd, "204 No Content", NULL);
    if (control.count == 1 || control.count % 50 == 0) {
        char line[128];
        snprintf(line, sizeof line, "<5>frame-sink: control %lu sequence=%u throttle=%d steering=%d\n",
                 control.count, control.sequence, throttle, steering);
        kmsg(line);
    }
}

static void serve_control(int fd, char *buf, size_t cap)
{
    char body[160];
    long age_ms = -1;
    if (control.count) {
        struct timeval now;
        gettimeofday(&now, NULL);
        age_ms = (now.tv_sec - control.at.tv_sec) * 1000L + (now.tv_usec - control.at.tv_usec) / 1000;
    }
    int b = snprintf(body, sizeof body, "count=%lu sequence=%u throttle=%d steering=%d age_ms=%ld\n",
                     control.count, control.sequence, control.throttle, control.steering, age_ms);
    int n = snprintf(buf, cap, "HTTP/1.1 200 OK\r\n%sContent-Type: text/plain\r\n"
                     "Content-Length: %d\r\n\r\n%s", cors, b, body);
    if (n > 0) send_all(fd, buf, (size_t)n);
}

static void serve_frame(int fd, char *buf, size_t cap)
{
    int in = open(FRAME, O_RDONLY);
    struct stat st;
    if (in < 0 || fstat(in, &st)) {
        if (in >= 0) close(in);
        reply(fd, "404 Not Found", NULL);
        return;
    }
    int n = snprintf(buf, cap, "HTTP/1.1 200 OK\r\n%sContent-Type: image/jpeg\r\n"
                     "Content-Length: %ld\r\n\r\n", cors, (long)st.st_size);
    if (send_all(fd, buf, (size_t)n) == 0) {
        ssize_t r;
        while ((r = read(in, buf, cap)) > 0 && send_all(fd, buf, (size_t)r) == 0) {}
    }
    close(in);
}

int main(int argc, char **argv)
{
    static char buf[4096];
    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_port = htons(argc > 1 ? atoi(argv[1]) : 8090),
                               .sin_addr.s_addr = htonl(INADDR_ANY)};
    int yes = 1;
    if (argc > 2 && inet_pton(AF_INET, argv[2], &addr.sin_addr) != 1) {
        fprintf(stderr, "frame-sink: bad bind address\n"); return 2;
    }
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0 || setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) ||
        bind(ls, (struct sockaddr *)&addr, sizeof addr) || listen(ls, 2)) {
        perror("frame-sink listen"); return 1;
    }
    printf("frame-sink: listening on port %d\n", ntohs(addr.sin_port)), fflush(stdout);
    for (;;) {
        int fd = accept(ls, NULL, NULL);
        if (fd < 0) { if (errno == EINTR) continue; perror("accept"); sleep(1); continue; }
        struct timeval tv = {.tv_sec = 5};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        size_t got;
        int hlen = read_headers(fd, buf, sizeof buf, &got);
        if (hlen < 0) { close(fd); continue; }
        if (!strncmp(buf, "OPTIONS ", 8)) {
            reply(fd, "204 No Content", NULL);
        } else if (!strncmp(buf, "POST /frame ", 12)) {
            long len = content_length(buf);
            memmove(buf, buf + hlen, got - (size_t)hlen);
            store_frame(fd, buf, sizeof buf, got - (size_t)hlen, len);
        } else if (!strncmp(buf, "POST /control ", 14)) {
            long len = content_length(buf);
            memmove(buf, buf + hlen, got - (size_t)hlen);
            store_control(fd, buf, got - (size_t)hlen, len);
        } else if (!strncmp(buf, "GET /control ", 13)) {
            serve_control(fd, buf, sizeof buf);
        } else if (!strncmp(buf, "GET /frame.jpg ", 15)) {
            serve_frame(fd, buf, sizeof buf);
        } else {
            reply(fd, "404 Not Found", NULL);
        }
        close(fd);
    }
}
