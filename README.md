# STM32F769 Discovery Linux bench

On this development branch, the [Android LiveKit camera gateway](gateway/README.md) implements the first media and joystick interfaces for the F769 port. The target-side control-frame diagnostic lives in `port/`.

Buildroot external tree for the STM32F769I-DISCO with 16 MiB SDRAM. U-Boot starts Linux 5.15.211 from microSD. The landscape LCD displays kernel messages and mirrors the interactive BusyBox shell. Type on the ST-LINK virtual COM port at 115200 8N1.

## Build on Linux or WSL
Install the standard Buildroot host prerequisites. Use a native Linux filesystem and a PATH without spaces. Starting from this repository's root:

```sh
external="$PWD/external"
git clone https://github.com/buildroot/buildroot.git ../f769-buildroot
cd ../f769-buildroot
git checkout d38b4915e9437c5492836914c0a0fff12b4f1031
make BR2_EXTERNAL="$external" f769_disco_dev_defconfig
make
```

Use output/images/sdcard.img as a whole-card image. The board needs the matching u-boot.bin at internal flash address 0x08000000. Verify the target and preserve any wanted firmware before provisioning. The LCD handoff assumes this U-Boot's 480x800 RGB565 framebuffer at 0xc0e00000. The kernel rotates text into 100 columns by 30 rows. The bench profile disables the M7 data cache for Ethernet DMA coherency and retains the display clocks at handoff.

## Bench checks
Connect a PC Ethernet interface at 10.77.77.1/30. Linux uses 10.77.77.2/30. Run f769-hello on serial, then ping 10.77.77.1 from the board and ping 10.77.77.2 from the PC. The bench configuration provides a local root console. Keep it on a private test link.

The source platform was tested on physical hardware with an SD boot, mirrored PTY, kernel log output, bidirectional ping and a verified multi-megabyte TCP transfer. This repository packages the components as one external tree. Buildroot, Linux, U-Boot and BusyBox are fetched by the build under their respective upstream licenses. The build starts from the upstream STM32F769 SD board configuration and carries board-specific patches in external/patches.
