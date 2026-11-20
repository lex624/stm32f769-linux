import test from 'node:test';
import assert from 'node:assert/strict';
import {frameSize, frameUrl, runFrameLoop} from './frames.js';

test('frames scale to 320 wide with even dimensions', () => {
  assert.deepEqual(frameSize(1920, 1080), [320, 180]);
  assert.deepEqual(frameSize(1080, 1920), [320, 568]);
  assert.deepEqual(frameSize(240, 160), [240, 160]);
  assert.equal(frameSize(0, 480), null);
});

test('board URL defaults to /frame and rejects other schemes', () => {
  assert.equal(frameUrl(' http://10.211.247.152:8090 '), 'http://10.211.247.152:8090/frame');
  assert.equal(frameUrl('http://10.211.247.152:8090/frame'), 'http://10.211.247.152:8090/frame');
  for (const bad of ['ftp://board/frame', 'javascript:alert(1)', 'not a url', 'http://u:p@board/frame'])
    assert.equal(frameUrl(bad), null);
});

test('control URL defaults to /control', () => {
  assert.equal(frameUrl('http://192.168.0.99:8090', '/control'), 'http://192.168.0.99:8090/control');
  assert.equal(frameUrl('http://192.168.0.99:8090/control', '/control'), 'http://192.168.0.99:8090/control');
});

test('loop posts one frame at a time and backs off after a failure', async () => {
  let inFlight = 0, maxInFlight = 0, calls = 0;
  const sleeps = [];
  const result = await runFrameLoop({
    grab: async () => 'jpeg',
    post: async () => {
      inFlight++; maxInFlight = Math.max(maxInFlight, inFlight);
      calls++;
      await null;
      inFlight--;
      if (calls === 2) throw new Error('board busy');
    },
    isRunning: () => calls < 4,
    sleep: async ms => { sleeps.push(ms); },
    now: () => 0,
    interval: 500,
  });
  assert.deepEqual(result, {sent: 3, failed: 1});
  assert.equal(maxInFlight, 1);
  assert.deepEqual(sleeps, [500, 1000, 500, 500]);
});
