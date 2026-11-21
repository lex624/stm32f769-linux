/* Bench endpoint. Feed one six-byte GATT value per record on stdin from
 * the transport adapter. Report the two axes for hardware bring-up. */
#include <stdint.h>
#include <stdio.h>

static int signed_le16(const uint8_t *p)
{
    int value = p[0] + 256 * p[1];
    return value >= 32768 ? value - 65536 : value;
}

int main(void)
{
    uint8_t frame[6];
    size_t n;
    while ((n = fread(frame, 1, sizeof(frame), stdin)) != 0) {
        if (n != sizeof(frame)) {
            fputs("truncated control frame\n", stderr);
            return 1;
        }
        int throttle = signed_le16(frame + 2);
        int steering = signed_le16(frame + 4);
        if (frame[0] != 1 || throttle < -1000 || throttle > 1000 ||
            steering < -1000 || steering > 1000) {
            fputs("invalid control frame\n", stderr);
            return 1;
        }
        printf("sequence=%u throttle=%d steering=%d\n",
               (unsigned)frame[1], throttle, steering);
        fflush(stdout);
    }
    return ferror(stdin) ? 1 : 0;
}
