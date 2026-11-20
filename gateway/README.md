# Android camera and Discovery control prototype

Android Chrome publishes the rear camera to a LiveKit room over Wi-Fi or cellular. Chrome can also connect as a BLE central and write joystick samples to a Discovery peripheral. The STM32F769 Linux side has a small C frame reader in port/f769-control.c. The IDB05A1 adapter and one-write GATT service are in bench/bluenrg-probe.c. See the bench instructions for hardware results.

## Camera bench
1. Install Node 22.12 or later. From gateway run `npm ci`, `npm test` and `npm run dev`.
2. With Android USB debugging authorized, run `adb -d reverse tcp:5173 tcp:5173` and open `http://localhost:5173` in Android Chrome. For untethered use serve the built dist directory over HTTPS.
3. Use a LiveKit room with short-lived participant tokens issued by a trusted server. Give the phone the identity **rover-cam**, with room join, camera publish and subscribe permission. Give the controller a different identity and permission to publish data. The server URL must be reachable from the phone. Keep the LiveKit API secret on the token issuer.
4. Enter the server URL, phone token and exact authorized controller identity. Select **Publish phone camera** if video is wanted, then tap **Join room**. Android asks for camera permission when video is selected. A second participant in the same room should receive the video. Disconnect stops publication. Microphone capture is not requested.
5. From the reference controller send a gamepad event. The phone displays the two axes even when no shield is attached. Root access is unnecessary.

## Wi-Fi control bench

When no BLE board is connected, the gateway can POST each control sample to the board's `frame-sink` instead. Set **Board control URL** to `http://<board>:8090/control`, for example the board's Wi-Fi address.
- **Body:** the same 6-byte frame used over BLE (see the contract table below).
- **Pacing:** the gateway drops new samples while a POST is pending, so a slow link never builds a queue.
- **Board side:** `frame-sink` rejects a wrong version or out-of-range axes with 400. It logs every 50th sample to the console.
- **Reading back:** `GET /control` returns the count, the last values and `age_ms`, the time since the last sample.

Anything that drives actuators must stop when `age_ms` grows.

Verified 20 November 2026 with LiveKit Server 1.13.11 on the LAN and a Python controller sending at 20 Hz for 60 s:
- **Throughput:** the controller sent 1177 samples. The Flip5 forwarded 1152 over Wi-Fi with no failures, skipping the rest while a POST was pending, and the board received all 1152.
- **Camera:** it published at about 28 fps throughout.

Use Chrome on Android: Firefox has no Web Bluetooth, and Chrome asks for the Nearby devices permission on the first BLE scan. With `adb reverse`, forward only the page port (5173) to the phone. Give the phone the server's LAN address: WebRTC from Android cannot use a candidate on 127.0.0.1 tunnelled by adb.

## BLE bench
The IDB05A1 carries an SPBTLE-RF network processor. The F769 must initialize its SPI transport, IRQ and reset, register the service below and advertise the service UUID before **Connect BLE board** will find it. Start the one-write diagnostic with `bluenrg-probe --serve` on the Discovery. The factory D3 clock wiring was verified on the bench. A generic pairing in Android settings does not install the service.

| Interface | Contract |
|---|---|
| Service | d6f4a610-12b8-4a6e-a194-f76900000001 |
| Control characteristic | d6f4a610-12b8-4a6e-a194-f76900000002, write with response |
| Value | Six bytes: version 1, sequence modulo 256, signed little-endian throttle and steering in thousandths |
| Room data | Topic controls, UTF-8 JSON, type gamepad, numerical data.left_y and data.right_x in [-1,1] |

Example value `01 01 18 FC F4 01` reports sequence 1, throttle -1000 and steering 500. Compile the diagnostic with the Buildroot arm-linux-gcc wrapper and -Wl,-elf2flt. It accepts binary frames on stdin. This branch only reports command values. Before any actuator integration, define peripheral authentication, disconnect behaviour and command expiry.

The controller message format and camera identity follow the [LiveKit rover example](https://github.com/livekit-examples/rover-teleop). The Raspberry Pi camera/GStreamer pipeline is replaced here by Android's camera and the LiveKit JavaScript SDK. [ST expansion board](https://www.st.com/en/evaluation-tools/x-nucleo-idb05a1.html)
