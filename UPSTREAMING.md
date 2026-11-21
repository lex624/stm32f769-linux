# Upstreaming plan (checked 2026-11-20)

The board's Linux comes from three projects. Buildroot builds the image. The board support comes from mainline Linux and U-Boot. Buildroot carries no STM32F769 patches of its own: its `stm32f769_disco_sd_defconfig` has Linux 5.15.222 and U-Boot 2026.10, plus hash files. Our fixes therefore go to Linux first. Buildroot picks them up through kernel bumps.

## Linux (mailing lists, no ticket tracker)

**Update (2026-11-20, after mainline 7.4-rc3 tests on the board):**
- **Ready for review:** a 4-patch series in `../upstream/linux-stm32-f769-ethernet-v1`, not yet sent:
  - enable `ARM_MPU` in `stm32_defconfig`;
  - add the F746 Ethernet MAC;
  - add the RMII pins;
  - enable Ethernet on the F769-disco.
- **Ethernet:** works with the D-cache on once the MPU is enabled. Without the MPU, the DMA pool is cached and transmit stalls.
- **Dropped:**
  - 0005 (dwc2 VBUS): CN15 works without it after a cold power-on.
  - 0006 (overcurrent): the Flip5 enumerates and stays connected without it.
  Both were 5.15-only problems.

The table below is the original plan.

| Our patch | Mainline state | Destination | Verdict |
|---|---|---|---|
| 0005 dwc2: `phy_ulpi_ext_vbus` for the F7 HS port | Missing in `dwc2_set_stm32f7_hsotg_params`. Applies at an offset of 98 lines. | linux-usb; Thinh Nguyen, Greg KH | **Send first.** A one-line bug fix. Add a commit message, a `Fixes:` tag and a Signed-off-by. |
| 0002 Ethernet node for `stm32f746.dtsi` plus the F769-disco | No MAC node in mainline `st/stm32f746.dtsi` | devicetree and linux-stm32; Alexandre Torgue, Maxime Coquelin | **Send second.** A DT series. Run `make dtbs_check` and rebase onto `arch/arm/boot/dts/st/`. |
| 0006 `disable-over-current` on `usbotg_hs` | Missing | Same series as 0002 | Probably true of every F769-disco, since the USB3320 reports VBUS-valid as overcurrent. Explain the reasoning in the commit. |
| 0007 rt2800usb RX queue 128 to 16 | Hard-coded 128 | linux-wireless | Not acceptable as written. At most an RFC that makes the limit depend on `!CONFIG_MMU`. |
| 0001 drop card-detect, 0003 stmmac rings, 0004 boot framebuffer | — | — | Keep local. 0001 works around this board's dead switch, and the other two are bench tuning. |

Before anything is sent:
- **Test on mainline.** Every patch has been tested only on 5.15.211, and maintainers expect a mainline test. Boot a mainline kernel on the board, from RAM through `loadx` so the card isn't touched.
- **Set the WSL git name.** `user.name` is currently `aosp`, but kernel and Buildroot patches need a real name in Signed-off-by.
- **Install git send-email.** It isn't installed, and installing it needs sudo with a password. Run `sudo apt install git-email`, then configure SMTP (a Gmail app password).

## Buildroot (GitLab issues, patches go to buildroot@buildroot.org)
Of the 90 open issues, none covers STM32, noMMU or FLAT. Nothing there is worth taking for this board. Once the Linux patches land, a defconfig bump picks them up. Patches go by git send-email, not as merge requests.

## Other noMMU tooling
- **elf2flt** (uclinux-dev/elf2flt): issue #11, Thumb-2 `R_ARM_THM_MOVW_ABS_NC`/`MOVT` relocations, has been open since 2020. We build Thumb-2 for Cortex-M7, so we can check whether our toolchain emits these relocations and report the result.
- **uClibc-ng**: in a linuxthreads build, `errno`, `raise` and `nanosleep` each link the thread manager (about 35 KB) into single-threaded programs (see `bench/lsh-errno.c`). This could be raised on the mailing list as a size issue for FLAT targets.

## LiveKit

What we use (both are the latest releases): `livekit-client` 2.24.1 in the Android Chrome gateway, and `livekit` 1.3.0 plus `livekit-api` 2.0.1 in `bench/livekit-smoke.py`. The gateway does three things:
- publishes the rear camera to a room;
- takes `controls` gamepad data and forwards it to the board as a 6-byte frame over Web Bluetooth GATT;
- posts JPEG frames to the board over USB tethering.

The smoke test sends LiveKit data to the board over SSH and checks the board's decoded reply.

Where we can contribute:
1. **python-sdks / rust-sdks:** not a bug. We suspected an early reliable `publish_data` was being lost, but a board-free repro delivered 160 of 160 messages, using LiveKit server 1.13.11 and SDK 1.3.0 locally. Nothing to file. The resend loop in `bench/livekit-smoke.py` rests on an assumption we never confirmed.
2. **livekit-examples/rover-teleop:** posted as livekit-examples/rover-teleop#5 on 2026-11-20. It proposes a phone-as-rover variant with control over BLE or Wi-Fi and no Pi. It is backed by the end-to-end runs in `gateway/README.md`. Follow-ups are in #1: latency, positioning against the ESP32 SDK, and telemetry.
3. **client-sdk-esp32** is the embedded SDK. #132 (use-after-free in `peer_destroy`) is assigned and already has PR #147. #104 (the TURN relay is never used on ESP32-S3) is unassigned but needs ESP32-S3 hardware.

## Prepared workspaces (WSL)
- `~/src/linux`: mainline at 65e4fd942 (2026-11-19), depth 1. `scripts/checkpatch.pl` and `get_maintainer.pl` work.
- `~/src/buildroot-upstream`: a git worktree of `~/stm32f769-linux/buildroot` on branch `contrib/upstream`, at master fe9b5cd0. The pinned build tree is unchanged.
