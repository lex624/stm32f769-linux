# Android camera and Discovery control prototype

Android Chrome publishes the rear camera to a LiveKit room over Wi-Fi or cellular. Chrome can also connect as a BLE central and write joystick samples to a Discovery peripheral. The STM32F769 Linux side has a small C frame reader in port/f769-control.c. The SPI/ACI adapter and GATT server for the IDB05A1 remain to be implemented and validated on the physical shield.

## Camera bench
1. Install Node 22.12 or later. From gateway run `npm ci`, `npm test` and `npm run dev`.
2. With Android USB debugging authorized, run `adb -d reverse tcp:5173 tcp:5173` and open `http://localhost:5173` in Android Chrome. For untethered use serve the built dist directory over HTTPS.
3. Use a LiveKit room with short-lived participant tokens issued by a trusted server. Give the phone the identity **rover-cam**, with room join, camera publish and subscribe permission. Give the controller a different identity and permission to publish data. The server URL must be reachable from the phone. Keep the LiveKit API secret on the token issuer.
4. Enter the server URL, phone token and exact authorized controller identity. Tap **Join and publish camera** and accept Android's camera permission. A second participant in the same room should receive the video. Disconnect stops publication. Microphone capture is not requested.
5. From the reference controller send a gamepad event. The phone displays the two axes even when no shield is attached. Root access is unnecessary.

## BLE bench
The IDB05A1 carries an SPBTLE-RF network processor. The F769 must initialize its SPI transport, IRQ and reset, register the service below and advertise the service UUID before **Connect BLE board** will find it. Board-to-shield wiring, solder bridge settings and Linux SPI ownership need verification on the Discovery. A generic pairing in Android settings does not install the service.

| Interface | Contract |
|---|---|
| Service | d6f4a610-12b8-4a6e-a194-f76900000001 |
| Control characteristic | d6f4a610-12b8-4a6e-a194-f76900000002, write with response |
| Value | Six bytes: version 1, sequence modulo 256, signed little-endian throttle and steering in thousandths |
| Room data | Topic controls, UTF-8 JSON, type gamepad, numerical data.left_y and data.right_x in [-1,1] |

Example value `01 01 18 FC F4 01` reports sequence 1, throttle -1000 and steering 500. Compile the diagnostic with the Buildroot arm-linux-gcc wrapper and -Wl,-elf2flt. It accepts binary frames on stdin. This branch only reports command values. Before any actuator integration, define peripheral authentication, disconnect behaviour and command expiry.

The controller message format and camera identity follow the [LiveKit rover example](https://github.com/livekit-examples/rover-teleop). The Raspberry Pi camera/GStreamer pipeline is replaced here by Android's camera and the LiveKit JavaScript SDK. [ST expansion board](https://www.st.com/en/evaluation-tools/x-nucleo-idb05a1.html)
