// Proposed bench GATT service. The BlueNRG peripheral must first expose it over SPI/ACI.
export const SERVICE = 'd6f4a610-12b8-4a6e-a194-f76900000001';
export const CONTROL = 'd6f4a610-12b8-4a6e-a194-f76900000002';
const decoder = new TextDecoder('utf-8', {fatal:true});

export function decodeControls(bytes, topic, sender, allowed) {
  if (topic !== 'controls' || !allowed || sender !== allowed || bytes.length > 2048) return null;
  try {
    const message = JSON.parse(decoder.decode(bytes));
    if (message?.type !== 'gamepad') return null;
    const values = [message.data?.left_y, message.data?.right_x];
    return values.every(v => Number.isFinite(v) && Math.abs(v) <= 1) ? values : null;
  } catch { return null; }
}

export function encodeFrame(sequence, values) {
  const frame = new DataView(new ArrayBuffer(6));
  frame.setUint8(0, 1); // format version
  frame.setUint8(1, sequence & 255);
  frame.setInt16(2, Math.round(values[0] * 1000), true);
  frame.setInt16(4, Math.round(values[1] * 1000), true);
  return new Uint8Array(frame.buffer);
}
