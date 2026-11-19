/* X-NUCLEO-IDB05A1 factory links. D3 (PF6) supplies SPI clock.
 * Request lines exclusively; abort if another Linux driver owns a pin. */
#include <fcntl.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int line(char bank, unsigned pin, int initial)
{
    char path[64];
    int chip, bankno = bank - 'A';
    struct gpiohandle_request req = {0};
    /* Confirm the STM32 GPIO bank by its device-tree register address. */
    snprintf(path, sizeof(path), "/sys/bus/gpio/devices/gpiochip%d/uevent", bankno);
    FILE *f = fopen(path, "r");
    char entry[192], expected[96];
    snprintf(expected, sizeof(expected), "OF_FULLNAME=/soc/pin-controller@40020000/gpio@%x\n",
             0x40020000u + 1024u * bankno);
    int matched = 0;
    while (f && fgets(entry, sizeof(entry), f)) {
        if (!strcmp(entry, expected)) matched = 1;
    }
    if (!matched) { fprintf(stderr, "Could not verify GPIO bank %c\n", bank); exit(1); }
    fclose(f);
    snprintf(path, sizeof(path), "/dev/gpiochip%d", bankno);
    chip = open(path, O_RDONLY);
    req.lines = 1;
    req.lineoffsets[0] = pin;
    req.flags = initial < 0 ? GPIOHANDLE_REQUEST_INPUT : GPIOHANDLE_REQUEST_OUTPUT;
    if (initial < 0) req.flags |= GPIOHANDLE_REQUEST_BIAS_PULL_DOWN;
    req.default_values[0] = initial > 0;
    strcpy(req.consumer_label, "idb05a1");
    if (chip < 0 || ioctl(chip, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        fprintf(stderr, "P%c%u: ", bank, pin); perror("GPIO request"); exit(1);
    }
    close(chip);
    return req.fd;
}
static void set(int fd, int bit)
{
    struct gpiohandle_data d = {0};
    d.values[0] = !!bit;
    if (ioctl(fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &d)) { perror("GPIO write"); exit(1); }
}
static int get(int fd)
{
    struct gpiohandle_data d;
    if (ioctl(fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &d)) { perror("GPIO read"); exit(1); }
    return d.values[0];
}
static int cs, clk, mosi, miso;
static unsigned exchange(unsigned byte)
{
    unsigned received = 0;
    for (int mask = 128; mask; mask >>= 1) {
        set(mosi, byte & mask);
        set(clk, 1);
        received = received * 2 + get(miso);
        set(clk, 0);
    }
    return received;
}
static int transfer(unsigned char *data, int len, int writing)
{
    unsigned h[5];
    /* BlueNRG may still be entering standby just after the preceding event. */
    set(cs, 0);

    h[0] = exchange(writing ? 0x0a : 0x0b);
    for (int i = 1; i < 5; i++) h[i] = exchange(0);
    static unsigned attempts;
    if (getenv("HCI_TRACE") && attempts++ < 12) fprintf(stderr,"header %02x %02x %02x %02x %02x\n",h[0],h[1],h[2],h[3],h[4]);
    int count = writing ? h[1] + 256*h[2] : h[3] + 256*h[4];
    if (h[0] != 2 || (writing && count < len)) count = 0;
    if (count > len) count = len;
    for (int i = 0; i < count; i++) {
        unsigned rx = exchange(writing ? data[i] : 0);
        if (!writing) data[i] = rx;
    }
    set(cs, 1);
    return count;
}
static int serve(int irq);

int main(int argc, char **argv)
{
    int serving = argc == 2 && !strcmp(argv[1], "--serve");
    if (argc != 1 && !serving) return 2;
    cs = line('A', 4, 1);
    int eeprom = line('J', 4, 1);
    int irq = line('A', 6, -1);
    clk = line('F', 6, 0);
    mosi = line('B', 15, 0);
    miso = line('B', 14, -1);
    int reset = line('J', 3, 0);
    for (int attempt = 0; attempt < 4; attempt++) {
        set(reset, 0);
        usleep(10000);
        set(reset, 1);
        usleep(50000);
        if (!serving) break;
        int result = serve(irq);
        if (result != 3) {
            set(reset, 0);
            usleep(10000);
            set(reset, 1);
            return result;
        }
    }
    if (serving) return 1;
    unsigned char data[255], version[] = {1, 1, 0x10, 0};
    int sent = 0;
    for (int i = 0; i < 100; i++) {
        if (get(irq)) {
            int n = transfer(data, sizeof(data), 0);
            if (n) {
                printf("HCI");
                for (int j = 0; j < n; j++) printf(" %02x", data[j]);
                puts("");
                if (n >= 15 && data[0]==4 && data[1]==14 && data[4]==1 &&
                    data[5]==0x10 && data[6]==0) return 0;
            }
        }
        if (!sent) sent = transfer(version, sizeof(version), 1);
        usleep(10000);
    }
    fprintf(stderr,"No version-complete event (IRQ=%d, command bytes accepted=%d)\n",get(irq),sent);
    close(eeprom);
    return 1;
}

/* Send a BlueNRG-MS HCI command and collect its matching Command Complete.
 * The return parameters begin at event byte 7, after the status. */
static unsigned last_name_handle;
static unsigned hci(int irq, unsigned opcode, const unsigned char *args, unsigned size)
{
    unsigned char request[64] = {1, opcode & 255, opcode >> 8, size}, event[255];
    if (size > sizeof(request) - 4) exit(2);
    if (size) memcpy(request + 4, args, size);
    int sent = 0, queued_version = 0;
    unsigned char keep_awake[] = {1,1,0x10,0};
    for (int i = 0; i < 150; i++) {
        if (!sent) sent = transfer(request, size + 4, 1);
        if (get(irq)) {
            /* Keep a benign version reply queued across setup commands. The
             * radio stays awake while the host has unread event data (UM1865). */
            if (sent && !queued_version)
                queued_version = transfer(keep_awake, sizeof(keep_awake), 1);
            int n = transfer(event, sizeof(event), 0);
            if (n && getenv("HCI_TRACE")) { fprintf(stderr,"rx"); for(int j=0;j<n;j++) fprintf(stderr," %02x",event[j]); fputc(10,stderr); }
            if (n >= 7 && event[0] == 4 && event[1] == 14 &&
                (event[4] | event[5] << 8) == opcode) {
                if (event[6]) {
                    fprintf(stderr, "HCI %04x status %02x\n", opcode, event[6]);
                    exit(1);
                }
                if (opcode == 0xfc8a && n >= 11) last_name_handle = event[9] | event[10] << 8;
                return n >= 9 ? event[7] | event[8] << 8 : 0;
            }
        }
        if (sent) usleep(1000);
    }
    fprintf(stderr, "HCI %04x timed out (wrote %d bytes, IRQ %d)\n", opcode, sent, get(irq));
    exit(1);
}

static int serve(int irq)
{
    /* UUIDs use Bluetooth little-endian byte order over the controller bus. */
    const unsigned char uuid[16] = {1,0,0,0,0x69,0xf7,0x94,0xa1,0x6e,0x4a,0xb8,0x12,0x10,0xa6,0xf4,0xd6};
    unsigned char service[19] = {2}, characteristic[25] = {0};
    unsigned char adv[36] = {0, 0xa0,0, 0xe0,0, 1,0, 5,9,'F','7','6','9',17,7};
    unsigned char gap[] = {1,0,4}, props[] = {6,8,0,1,16,0}, scan[32] = {6,5,9,'F','7','6','9'};
    hci(irq, 0xfd01, NULL, 0); /* GATT Init */
    unsigned gap_service = hci(irq, 0xfc8a, gap, sizeof(gap)); /* GAP peripheral, privacy disabled */
    unsigned char name[] = {gap_service, gap_service >> 8, last_name_handle,
                            last_name_handle >> 8, 0,4,'F','7','6','9'};
    hci(irq, 0xfd06, name, sizeof(name));
    memcpy(service + 1, uuid, 16);
    service[17] = 1; service[18] = 4;
    unsigned svc = hci(irq, 0xfd02, service, sizeof(service));
    characteristic[0] = svc; characteristic[1] = svc >> 8; characteristic[2] = 2;
    memcpy(characteristic + 3, uuid, 16);
    characteristic[3] = 2;
    memcpy(characteristic + 19, props, sizeof(props));
    unsigned value_handle = 1 + hci(irq, 0xfd04, characteristic, sizeof(characteristic));
    memcpy(adv + 15, uuid, 16);
    hci(irq, 0x2009, scan, sizeof(scan)); /* scan response supplies the name */
    hci(irq, 0xfc83, adv, 35);
    fprintf(stderr, "F769 advertising; waiting for one six-byte write\n");
    /* Stop advertising after one complete diagnostic; the next run resets the radio. */
    for (int i = 0; i < 18000; i++) {
        unsigned char event[255];
        int n = get(irq) ? transfer(event, sizeof(event), 0) : 0;
        if (n && getenv("HCI_TRACE")) {
            fprintf(stderr, "event");
            for (int j = 0; j < n; j++) fprintf(stderr, " %02x", event[j]);
            fputc(10, stderr);
        }
        if (n >= 7 && event[0] == 4 && event[1] == 5) return 3;
        if (n == 18 && event[0] == 4 && event[1] == 255 &&
            event[3] == 1 && event[4] == 12 && (event[7] | event[8]<<8) == value_handle &&
            event[9] == 6 && event[10] == 0 && event[11] == 0) {
            if (event[12] != 1) return 1;
            int throttle = (short)(event[14] | event[15]<<8);
            int steering = (short)(event[16] | event[17]<<8);
            if (throttle < -1000 || throttle > 1000 || steering < -1000 || steering > 1000) return 1;
            printf("sequence=%u throttle=%d steering=%d\n", event[13], throttle, steering);
            return 0;
        }
        usleep(10000);
    }
    fprintf(stderr, "Timed out waiting for a GATT write\n");
    return 1;
}
