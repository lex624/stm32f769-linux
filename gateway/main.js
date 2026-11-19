import {Room, RoomEvent} from 'livekit-client';
import {SERVICE, CONTROL, decodeControls, encodeFrame} from './controls.js';

const el = id => document.getElementById(id);
let active, device, characteristic, sequence = 0, writing = false;

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
window.addEventListener('pagehide', () => { active?.disconnect(); device?.gatt.disconnect(); });

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
