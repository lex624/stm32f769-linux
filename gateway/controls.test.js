import test from 'node:test';
import assert from 'node:assert/strict';
import {decodeControls, encodeFrame} from './controls.js';
const packet = (y,x) => new TextEncoder().encode(JSON.stringify({type:'gamepad',data:{left_y:y,right_x:x}}));
test('upstream controls packet and GATT byte order', () => {
  const axes = decodeControls(packet(-1,0.5),'controls','pilot','pilot');
  assert.deepEqual(axes,[-1,0.5]);
  assert.deepEqual([...encodeFrame(257,axes)],[1,1,24,252,244,1]);
});
test('ignore other participants, topics and malformed axes', () => {
  for (const [bytes,topic,sender] of [[packet(0,0),'imu','pilot'],[packet(0,0),'controls','other'],
    [packet('1',0),'controls','pilot'],[packet(2,0),'controls','pilot'],[Uint8Array.of(255),'controls','pilot'],
    [new Uint8Array(2049),'controls','pilot']]) {
    assert.equal(decodeControls(bytes,topic,sender,'pilot'),null);
  }
});
