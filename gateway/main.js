import {Room, RoomEvent, Track} from 'livekit-client';
import {SERVICE, CONTROL, decodeControls, encodeFrame} from './controls.js';
import {FRAME_QUALITY, frameSize, frameUrl, runFrameLoop} from './frames.js';

const el = id => document.getElementById(id);
let active, device, characteristic, sequence = 0, writing = false;
let frameRun;

function resetRoom(room) {
  if (active !== room) return;
  active = undefined;
  el('preview').srcObject = null;
  el('preview').hidden = true;
  el('join').disabled = false;
  el('leave').disabled = true;
  el('status').textContent = 'Disconnected';
  el('axes').textContent = 'Waiting for control data';
}

el('connect').onsubmit = async event => {
  event.preventDefault();
  if (active) return;
  const allowed = el('controller').value.trim();
  if (!allowed) return;
  const room = new Room();
  active = room;
  el('join').disabled = true;
  el('status').textContent = 'Connecting…';
  room.on(RoomEvent.Disconnected, () => resetRoom(room));
  room.on(RoomEvent.DataReceived, async (bytes, participant, kind, topic) => {
    if (active !== room) return;
    const axes = decodeControls(bytes, topic, participant?.identity, allowed);
    if (!axes) return;
    el('axes').textContent = 'Throttle ' + axes[0] + ' · Steering ' + axes[1];
    // Drop samples during a pending write; delayed joystick events should not queue.
    if (!characteristic || writing) return;
    writing = true;
    try { await characteristic.writeValueWithResponse(encodeFrame(sequence++, axes)); }
    catch { device?.gatt.disconnect(); }
    finally { writing = false; }
  });
  try {
    await room.connect(el('url').value.trim(), el('token').value.trim());
    el('token').value = '';
    if (el('camera').checked) {
      const pub = await room.localParticipant.setCameraEnabled(true, {facingMode:'environment'});
      if (!pub?.track || active !== room) throw new Error('Camera unavailable');
      pub.track.attach(el('preview'));
      el('preview').hidden = false;
    }
    el('leave').disabled = false;
    el('status').textContent = 'Connected as ' + room.localParticipant.identity;
  } catch {
    await room.disconnect();
    resetRoom(room);
    el('status').textContent = 'Connection or camera failed. Check room credentials, network and camera permission.';
  }
};
el('leave').onclick = () => active?.disconnect();
el('ble').onclick = async () => {
  if (device?.gatt.connected) { device.gatt.disconnect(); return; }
  if (!navigator.bluetooth) { el('board').textContent = 'Web Bluetooth unavailable in this browser'; return; }
  el('ble').disabled = true;
  try {
    device = await navigator.bluetooth.requestDevice({filters:[{services:[SERVICE]}]});
    device.addEventListener('gattserverdisconnected', () => {
      el('sample').disabled = true;
      characteristic = undefined;
      el('ble').textContent = 'Connect BLE board';
      el('board').textContent = 'Board disconnected';
    }, {once:true});
    const server = await device.gatt.connect();
    const service = await server.getPrimaryService(SERVICE);
    characteristic = await service.getCharacteristic(CONTROL);
    if (!characteristic.properties.write) throw new Error('Missing write-with-response support');
    sequence = 0;
    el('sample').disabled = false;
    el('ble').textContent = 'Disconnect BLE board';
    el('board').textContent = 'Connected to ' + (device.name || 'Discovery');
  } catch {
    device?.gatt.disconnect();
    characteristic = undefined;
    el('board').textContent = 'No compatible control service connected';
  } finally { el('ble').disabled = false; }
};
// Reuse the published LiveKit camera when present; Android rarely opens the camera twice.
async function cameraTrack() {
  const published = active?.localParticipant.getTrackPublication(Track.Source.Camera)?.track?.mediaStreamTrack;
  if (published?.readyState === 'live') return {track: published, owned: false};
  const stream = await navigator.mediaDevices.getUserMedia({video: {facingMode: 'environment'}, audio: false});
  return {track: stream.getVideoTracks()[0], owned: true};
}

function stopFrames() {
  if (!frameRun) return;
  frameRun.running = false;
  if (frameRun.owned) frameRun.track.stop();
  frameRun.video.srcObject = null;
  frameRun = undefined;
  el('frames').textContent = 'Send frames to board';
}

el('frames').onclick = async () => {
  if (frameRun) { stopFrames(); el('frameStatus').textContent = 'Stopped'; return; }
  const url = frameUrl(el('frameUrl').value);
  if (!url) { el('frameStatus').textContent = 'Enter the board http:// frame URL'; return; }
  el('frames').disabled = true;
  try {
    const {track, owned} = await cameraTrack();
    const video = document.createElement('video');
    video.muted = true; video.playsInline = true;
    video.srcObject = new MediaStream([track]);
    await video.play();
    const run = frameRun = {running: true, track, owned, video};
    const canvas = document.createElement('canvas');
    const grab = () => new Promise(resolve => {
      const size = frameSize(video.videoWidth, video.videoHeight);
      if (!size) { resolve(null); return; }
      [canvas.width, canvas.height] = size;
      canvas.getContext('2d').drawImage(video, 0, 0, ...size);
      canvas.toBlob(resolve, 'image/jpeg', FRAME_QUALITY);
    });
    const post = async blob => {
      const res = await fetch(url, {method: 'POST', body: blob, headers: {'Content-Type': 'image/jpeg'},
        signal: AbortSignal.timeout(4000), targetAddressSpace: 'private'});
      if (!res.ok) throw new Error('Board answered ' + res.status);
    };
    el('frames').textContent = 'Stop sending frames';
    el('frameStatus').textContent = 'Starting…';
    runFrameLoop({grab, post, isRunning: () => run.running,
      onResult: ({sent, failed, error}) => {
        if (frameRun === run) el('frameStatus').textContent =
          `Sent ${sent} frames, ${failed} failed` + (error ? ' (last: ' + error.message + ')' : '');
      }}).finally(() => { if (frameRun === run) stopFrames(); });
  } catch (error) {
    stopFrames();
    el('frameStatus').textContent = 'Camera or board unavailable: ' + error.message;
  } finally { el('frames').disabled = false; }
};

window.addEventListener('pagehide', () => { stopFrames(); active?.disconnect(); device?.gatt.disconnect(); });

el('sample').onclick = async () => {
  if (!characteristic || writing) return;
  writing = true;
  try {
    await characteristic.writeValueWithResponse(encodeFrame(42, [0.25, -0.5]));
    el('board').textContent = 'Board acknowledged test axes';
  } catch {
    el('board').textContent = 'Test write failed';
  } finally { writing = false; }
};
