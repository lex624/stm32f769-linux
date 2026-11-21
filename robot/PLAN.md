# Robot test suite plan: STM32F769I-DISCO Linux bench

The goal is to turn the manual bench checks into a Robot Framework suite. It runs in two places:
- **GitHub-hosted runners**, for checks that need no hardware.
- **A self-hosted runner on the Dell**, which has the board (COM9 serial, bench Ethernet), the Wi-Fi dongle, the BLE shield and the Flip5.

We build it one section at a time. Each section starts with a single simple test and grows from there.

## Conventions (same style as our existing system-test suites)
- **Layout:** `robot/suites/F769_Test_Suite/` holds numbered suite files (`10_...`, `20_...`) and an `__init__.robot` with the suite-wide setup and teardown. Shared keywords live in `robot/resources/*.robot`, Python helpers in `robot/lib/*.py`, and the runner in `robot/run_robot.py`.
- **Files:** each file has `#~~~~#` banners around its sections and uses 3-space separators. Each suite sets `Force Tags`, a `Test Timeout` and a named `Suite Setup`.
- **Test names:** an ID followed by an upper-case title, for example `BLD-01 GATEWAY UNIT TESTS PASS`. IDs use one prefix per section, listed under "Sections".
- **Tags:**
  - *Cadence:* `premerge` (no hardware, every push and PR), `postmerge`, `nightly`, `weekend`. Reliability loops read their iteration count from the cadence tag.
  - *Equipment:* `needsboard`, `needsserial`, `needsethernet`, `needswifi`, `needsphone`, `needsbleshield`, `needsflashdrive`, `needslivekit`, `needssensortile`. Excluding one, for example `-e needsphone`, skips every test that needs that equipment.
  - *Weight:* `critical` and `noncritical`.
  - *Tracking:* the issue the test covers, such as `GH-1` or `GH-2`.
- **Missing hardware:** suite setup checks the hardware is there. It calls `Fatal Error` when a required device is missing; it does not let every test fail one by one.
- **Fallbacks:** try the preferred path first, then a fallback, and log a `WARN` when the fallback is used. For example: run over SSH, fall back to the serial console; reboot via sysrq, fall back to U-Boot reset.
- **Bench variables:** a YAML variables file per bench (`robot/benches/dell.yaml`) holds the COM port, board and phone addresses, adb serial and LiveKit URL. It contains no secrets: the LiveKit key is generated for each run and Wi-Fi credentials stay on the card.

## Board facts the keywords must encode
These are the lessons from bring-up. Each one becomes a keyword, so no test has to know about it:
- **Memory:** every exec needs one contiguous block, and BusyBox needs 256 KB. `Free Blocks Of Order  6` reads `/proc/buddyinfo`. Board checks prefer hush built-ins (`read`, `echo`, globbing), which work even when nothing can exec.
- **SSH:** roughly one dropbear session per boot. `Run On Board` uses SSH, which logs in to `lsh`, and falls back to serial.
- **Writes:** `Sync Board Storage` sends sysrq `s` and waits for "Emergency Sync complete". It must run before any reboot that follows a write.
- **Kernel loading:** `Load Kernel To RAM` is U-Boot `loadx` over XMODEM, followed by a `crc32` check. It never touches the card.
- **Phone:** screenshots need `-d <display id>`. Check the foreground app before acting, so we never disturb someone using the phone.

## Sections
| # | Suite | ID | Runs on | First test (the "simple start") | Grows into |
|---|---|---|---|---|---|
| 10 | `10_Build_Tests` | BLD | hosted (`premerge`) | gateway `npm ci && npm test && npm run build` | host `-Werror` builds of `bench/*.c`, `port/*.c` and `lcd-shell`; shellcheck; ruff; secret scan; `frame-sink` contract tests on the host; nightly Buildroot image build with size gates (`lsh` fits 128 KB, flat-binary exec images) |
| 20 | `20_Install_And_Load_Tests` | INS | Dell (`needsboard needsserial`) | board answers on serial, shows the `uname -r` expected for the build | stop U-Boot, `loadx` a kernel to RAM, CRC check, boot to shell, card-deploy path, reliability loop (reboot N times, back at shell each time) |
| 30 | `30_Platform_Tests` | PLT | Dell | `frame-sink`, `usb-tether`, `ssh-listener` running (read `/proc/*/comm`) | no oops or panic in the boot log; memory budget (order-6 block free after boot, 128 KB blocks available); `lsh` is root's shell |
| 40 | `40_Connectivity_Tests` | NET | Dell (`needsethernet`, `needswifi`) | ping 10.77.77.2 from the PC | SSH login reliability N/N on Ethernet and Wi-Fi, WPA2 association and DHCP at boot, `GET /control` reachable over Wi-Fi |
| 50 | `50_USB_Host_Tests` | USB | Dell (`needsphone`, `needsflashdrive`) | CN15 device enumerated at 480 Mb/s (sysfs `speed`) | flash drive mounts and reads, phone tethering brings `usb0` up, keyboard input (`noncritical`, manual) |
| 60 | `60_Control_Interface_Tests` | CTL | hosted and Dell | `POST /control` valid frame returns 204, `GET /control` matches | invalid frames return 400; one test vector set shared by JS `encodeFrame` and the C decoders (`frame-sink`, `f769-control`, `bluenrg-probe`); BLE one-write through the phone (`needsbleshield needsphone`) |
| 65 | `65_BLE_Telemetry_Tests` | TEL | Dell (`needsbleshield needssensortile`) | SensorTile (STM32L476JG) connects as central and the board decodes the 6-byte diagnostic frame | all six v2 telemetry types arrive within N cycles with increasing sequence numbers; telemetry readable on the board and forwarded to the room (#4, #1). Never writes the SensorTile motor mailbox |
| 70 | `70_LiveKit_End_To_End_Tests` | LKE | Dell (`needslivekit needsphone`) | phone joins as `rover-cam`, controller decodes camera frames | Wi-Fi control at 20 Hz for 60 s (board count equals phone forwarded count, 0 failures), BLE path, control and video latency (#1) |
| 80 | `80_Actuator_Tests` | ACT | Dell (future) | ADC reads the potentiometer in range | PWM duty follows throttle, motor stops when `age_ms` exceeds timeout (#2) |
| 99 | `99_Final_Tests` | FIN | Dell | collect boot log, `/proc/buddyinfo`, board and phone screenshots | restore default boot entry, remove adb forwards, stop the LiveKit server |

## Resources and libraries
- **`common.robot`:** imports, global constants, suite setup and teardown, reading the cadence tags.
- **`serial.robot`** plus `lib/serial_console.py` (pyserial):
  - `Stop At U-Boot`, `Run On Console`, `Wait For Shell`;
  - `Load Kernel To RAM`, using the `xmodem` package from PyPI instead of a hand-rolled sender;
  - `Sync Board Storage`, `Reboot Board`.
- **`board.robot`** (SSHLibrary):
  - `Run On Board`, with the serial fallback;
  - `Read Board File` (built-in `read`), `Free Blocks Of Order`, `Process Should Be Running`.
- **`phone.robot`** plus `lib/chrome_devtools.py`:
  - adb connect and foreground check, `Open Gateway In Chrome`, `Fill Gateway Form`, `Click Gateway Button`;
  - `Take Phone Screenshot` (display id).
- **`livekit.robot`** plus `lib/livekit_bench.py`:
  - start and stop `livekit-server` on the LAN with a key generated for each run;
  - start and stop the controller, and collect its counters.
- **`frame.robot`** plus `lib/control_frame.py`: encode and decode the 6-byte frame, and the shared test vectors.
- **Runner `run_robot.py`:**
  - `-i` and `-e` for tag include and exclude, `-V NAME:value` to override variables, `-D` for debug;
  - `-b <bench.yaml>` picks the bench;
  - writes `output/<timestamp>/` containing `log.html`, `report.html` and `output.xml`.

## GitHub Actions
1. **`premerge.yml`** (GitHub-hosted `ubuntu-latest`, on every push and PR):
   - installs Robot Framework and runs `robot -i premerge -e needs*`, so sections 10 and 60 on the host;
   - uploads `log.html` and `report.html` as artifacts;
   - this job is the first thing to build.
2. **`hil.yml`** (self-hosted Dell runner, labels `self-hosted, windows, f769-bench`):
   - **Triggers:** `workflow_dispatch` (choose a tag set) and a nightly schedule.
   - **Lock:** `concurrency: f769-bench`, so only one run uses the bench at a time.
   - **Security:** the repo is public, so this workflow must **never** run on `pull_request`, or anyone's fork could run code on the Dell. Use only dispatch, schedule, and push to `main` from the owner. Also turn on "Require approval for all outside collaborators" in the repo's Actions settings.
   - **Preconditions:** the runner fails fast if COM9, the board or the phone is missing; the `Fatal Error` from suite setup reports which.
   - **Outputs:** uploads the Robot artifacts plus the console logs.
3. **Later:** a nightly or on-demand Buildroot image build, either hosted with a cache (it takes over an hour) or in WSL on the Dell. It publishes `sdcard.img` and feeds section 20.

## Phases (one section at a time)
| Phase | Delivers | Done when |
|---|---|---|
| 0 | skeleton: layout, `common.robot`, runner, bench YAML, `premerge.yml` with `BLD-01` | a green premerge run on GitHub with `log.html` attached |
| 1 | section 10 complete (host builds, lint, `frame-sink` host contract) | premerge catches a deliberately broken C build and a malformed frame |
| 2 | self-hosted runner, `hil.yml`, section 20 (`INS-01` serial and version, then RAM-boot via `loadx`) | dispatch run boots a kernel from RAM and reaches the shell |
| 3 | sections 30 and 40 | nightly run reports the memory budget and SSH reliability |
| 4 | sections 50 and 60 on hardware | USB and control contract green on the bench |
| 4b | section 65, SensorTile telemetry (#4) | all six telemetry types decoded on a dispatch run |
| 5 | section 70 | E2E Wi-Fi control test passes from a dispatch run; latency numbers recorded for #1 |
| 6 | section 80, after the motor work in #2 | actuator tests green |

## Open decisions
1. **Runner OS:** Windows Python on the Dell, or WSL? COM9 and adb are native on Windows, so Windows is simpler.
2. **Board image under test:** every HIL run can RAM-boot the freshly built kernel, which is fast to recover, or deploy to the card, which is closer to real use. Suggestion: RAM-boot for `premerge` and `postmerge`, card deploy for `weekend`.
3. **Suite location:** keep the suite in this repo (`robot/`), or reuse `lex624/devicelab`'s adb and Robot keywords as a dependency.
