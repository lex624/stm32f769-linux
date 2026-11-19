# Discovery hardware validation

## Ethernet SSH

On the configured bench laptop, run `ssh f769-disco`. The board is 10.77.77.2/30 and the laptop USB Ethernet adapter is 10.77.77.1/30. The server accepts one key-authenticated session at a time. Exit the shell before starting the automated smoke test.

For a newly built SD card, create /etc/dropbear and /root/.ssh on the board. Generate a unique host key using `dropbearkey -t ed25519 -f /etc/dropbear/dropbear_ed25519_host_key`. Install your SSH public key as /root/.ssh/authorized_keys with permissions 0600 and set the directory to 0700. Check the host fingerprint from the physical console using `dropbearkey -y -f /etc/dropbear/dropbear_ed25519_host_key`. Connect with `ssh root@10.77.77.2` and compare the fingerprint on first use. The host key and authorized workstation keys belong on the individual board.

BusyBox init respawns ssh-listener. It binds the bench address, accepts one client and replaces itself with Dropbear in inetd mode. This avoids a second resident SSH server on the 16 MiB no-MMU board. The target-specific patch preserves the parent SIGCHLD handler when a vfork child launches a shell. This matters for both interactive sessions and remote commands.

## CN15 OTG

The DWC2 driver registers a USB host root hub and a gadget controller at 40040000.usb. Check with `ls /sys/bus/usb/devices` and `ls /sys/class/udc`. The image includes USB HID, webcam support and configfs ACM/ECM functions. Use the cable appropriate to the host or device role. A gadget function must be configured and bound to the UDC before the laptop will enumerate it. Enumeration of an external CN15 device is a separate test from registering the controller.

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
