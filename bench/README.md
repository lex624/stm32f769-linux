# Discovery hardware validation

## Ethernet SSH

On the configured bench laptop, run `ssh f769-disco`. The board is 10.77.77.2/30 and the laptop USB Ethernet adapter is 10.77.77.1/30. The server accepts one key-authenticated session at a time. Exit the shell before starting the automated smoke test.

For a newly built SD card, create /etc/dropbear and /root/.ssh on the board. Generate a unique host key using `dropbearkey -t ed25519 -f /etc/dropbear/dropbear_ed25519_host_key`. Install your SSH public key as /root/.ssh/authorized_keys with permissions 0600 and set the directory to 0700. Check the host fingerprint from the physical console using `dropbearkey -y -f /etc/dropbear/dropbear_ed25519_host_key`. Connect with `ssh root@10.77.77.2` and compare the fingerprint on first use. The host key and authorized workstation keys belong on the individual board.

BusyBox init respawns ssh-listener. It binds the bench address, accepts one client and replaces itself with Dropbear in inetd mode. This avoids a second resident SSH server on the 16 MiB no-MMU board. The target-specific patch preserves the parent SIGCHLD handler when a vfork child launches a shell. This matters for both interactive sessions and remote commands.

## CN15 OTG

The DWC2 driver registers a USB host root hub and a gadget controller at 40040000.usb. Check with `ls /sys/bus/usb/devices` and `ls /sys/class/udc`. The image includes USB HID, webcam support and configfs ACM/ECM functions. Use the cable appropriate to the host or device role. A gadget function must be configured and bound to the UDC before the laptop will enumerate it. Enumeration of an external CN15 device is a separate test from registering the controller.

Host mode needs two board fixes, both in external/patches/linux. Patch 0005 sets GUSBCFG.ULPIEVBUSD so the USB3320 drives the external VBUS switch, as ST's HAL does for this port. Without it LD5 never lights. Patch 0006 adds `disable-over-current` to the OTG node. The PHY reports VBUS-valid as the overcurrent signal, so a device's inrush made the core drop port power and the hub retry about once a second; LD5 flashed and nothing enumerated. The board's current-limited switch, with LD4 as its fault LED, still protects the port. Use a Micro-B OTG adapter with ID grounded. Verified 19 November 2026: a Galaxy Z Flip5 enumerated at high speed as `1-1`.

The Flip5 firmware offers no USB webcam mode, as `ro.usb.uvc.enabled` is unset, so it never presents a UVC device. Camera frames use USB tethering instead. `/etc/init.d/S50usb-tether` starts `frame-sink` on port 8090 and `usb-tether` at boot. Select **USB tethering** on the phone; `rndis_host` creates `usb0`, and `usb-tether` (an rtnetlink listener, idle otherwise) brings it up, runs `udhcpc` once and prints `usb-tether: usb0 up at <address>, gateway frame URL http://<address>:8090/frame` on the LCD and serial console. It does this again each time the phone re-enumerates. In the gateway, set **Board frame URL** to that URL and select **Send frames to board**. The board keeps the latest JPEG at /tmp/frame.jpg and serves it at `/frame.jpg`. Verified with the Flip5 rear camera at 320 pixels wide, about 2 frames per second, with no failures. The phone must load the gateway from `localhost` through `adb reverse`, because Chrome only opens the camera in a secure context. Restarting adbd, for example with `adb tcpip`, re-enumerates the phone and turns tethering off. After a long session, noMMU fragmentation can stop BusyBox from starting: exec needs a contiguous 256 KiB block. Check `/proc/buddyinfo`: if the order-6 column is zero, `echo 3 > /proc/sys/vm/drop_caches` usually restores it; otherwise reboot. Copying multi-megabyte files through the page cache is the main cause. The `usb-host` boot entry loads a kernel and DTB built from source with patches 0005 and 0006.

A USB keyboard on CN15 types into the LCD shell. `usbhid` and `hid-generic` are built in, and the VT keyboard handler feeds `/dev/tty1`, which `lcd-shell` reads in raw mode alongside the UART. Verified with a Telink 2.4 GHz keyboard and mouse receiver (248a:8327, full speed), plugged in at boot. `CONFIG_INPUT_EVDEV` is off, so there are no `/dev/input/event*` nodes; the keyboard reaches the console but programs can't read it as an input device, and the mouse does nothing.

USB flash drives use `usb-storage` with FAT/VFAT, from the storage block at the end of `external/usb.fragment`. On the bench card this kernel is the `usb-storage` boot entry. A Transcend JetFlash 4 GB was hot-plugged, enumerated at high speed as `sda` and mounted with `mount -r -t vfat /dev/sda1 /mnt/usb`. Reading a 36 MB file took 5.8 s, about 6.2 MB/s. Drop the page cache after large reads, since cached pages fragment SDRAM. Copying a large file to the card also leaves no block big enough for dropbear to start, so SSH fails until a reboot.

Wi-Fi uses a Ralink RT5370 dongle (148f:5370, RT5390 MAC with RF5370) on `rt2800usb`, from the optional `external/wifi.fragment`. The fragment adds about 616 KB of kernel RAM: available memory drops from 9580K to 8964K, so it is not in the default fragment list. On the bench card this kernel is the `usb-wifi` boot entry, which is the default and includes storage and HID. It needs `/lib/firmware/rt2870.bin` (8 KB, version 0.36). `wifi-scan` (bench/wifi-scan.c) lists networks through nl80211 with no `iw` or libnl. It brings `wlan0` up, which loads the firmware, triggers a scan, waits for the scan event and prints BSSID, frequency, signal and SSID. It found 8 networks on the bench. For WPA2, `wpa_supplicant` 2.11 builds for noMMU with static libnl 3.12, `CONFIG_TLS=internal` and `-Dfork=vfork`. The flat binary is 373 KB (281 KB text, 77 KB data). Its single allocation needs a free order-7 (512 KB) block, which is only there just after boot, so start it early from an init script, before frame-sink and dropbear. `wpa_supplicant -v` ran on a fresh boot; association is untested. Run it with `start-stop-daemon -b`, not `-B`, because `daemon()` needs fork. Then run `udhcpc -i wlan0`.

Deploying large files: an `echo s > /proc/sysrq-trigger` emergency sync only queues writeback. Rebooting before it prints `Emergency Sync complete` lost the last 37 KB of a 3.5 MB kernel while keeping the file's full size, so the board boot-looped. Check a kernel from U-Boot before relying on it: `load mmc 0:1 0xc0008000 /boot/<zImage>` must report the full size, and `crc32 0xc0008000 <size>` must match `zlib.crc32` on the PC. U-Boot's `loadx` (XMODEM over the ST-LINK serial port, about 5 minutes for 3.5 MB) can test-boot a kernel from RAM without touching the card.

The root boot.scr moves the device tree to 0xc0a00000, beyond the decompressor's scratch area, then starts the extlinux menu. On the original bench card the usb-bench, lcd-bench and original Linux entries are available. The first two retain the LCD and serial consoles.

## Minimum LiveKit round trip

Use Python 3.11 or later with `python -m pip install -r bench/requirements.txt`. Start a local LiveKit server on the laptop's bench Ethernet interface and set LIVEKIT_API_KEY and LIVEKIT_API_SECRET for the server and test. With LiveKit Server 1.13.11 and PowerShell:

```powershell
$env:LIVEKIT_API_KEY = 'localbench'
$env:LIVEKIT_API_SECRET = [guid]::NewGuid().ToString('N')
$env:LIVEKIT_KEYS = 'localbench: ' + $env:LIVEKIT_API_SECRET
Start-Process livekit-server -ArgumentList '--bind','10.77.77.1','--node-ip','10.77.77.1'
python bench/livekit-smoke.py --url ws://10.77.77.1:7880 --ssh f769-disco
```

Two laptop participants join a temporary LiveKit room. The controller publishes a gamepad message over a reliable WebRTC data channel. The receiving gateway checks its sender, topic and content, encodes six bytes, then launches f769-control over wired SSH. The hardware acknowledgement must contain `sequence=42 throttle=250 steering=-500`. This exercises the Linux board's Ethernet, SSH server and C decoder. The laptop handles ICE, DTLS and SCTP.

## BlueNRG-MS

With the X-NUCLEO-IDB05A1 attached, `bluenrg-probe` resets the radio and reads its HCI version. The verified factory pin assignments are A0/PA6 IRQ, A1/PA4 chip select, D3/PF6 clock, D7/PJ3 reset, D8/PJ4 EEPROM deselect, D11/PB15 MOSI and D12/PB14 MISO. The diagnostic claims GPIO lines exclusively and clocks SPI from userspace.

Run `bluenrg-probe --serve` for a bounded, one-write test. It exposes the service and characteristic in gateway/README.md and advertises as F769. In the phone gateway tap **Connect BLE board**, select the device and tap **Send test axes**. The on-board process prints the decoded sample and exits. It controls no actuators. Set HCI_TRACE=1 to print controller responses and link events.

On the physical bench, the controller returned HCI version 7, manufacturer 0x0030. Windows saw the service UUID over the air and the radio reported a successful LE connection. Windows subsequently disconnected during service discovery; a confirmed application write is required to validate this last step.

[ST BlueNRG-MS command and SPI reference](https://www.st.com/resource/en/user_manual/um1865-the-bluenrgms-bluetooth-le-stack-application-command-interface-aci-stmicroelectronics.pdf)
