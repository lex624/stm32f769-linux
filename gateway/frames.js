// JPEG frame upload to the Discovery's frame-sink over USB tethering on CN15.

export const FRAME_WIDTH = 320;
export const FRAME_INTERVAL_MS = 500;
export const FRAME_QUALITY = 0.7;

// Scale to at most maxWidth while keeping the aspect ratio; even sizes suit JPEG.
export function frameSize(width, height, maxWidth = FRAME_WIDTH) {
  if (!(width > 0 && height > 0)) return null;
  const scale = Math.min(1, maxWidth / width);
  const even = v => Math.max(2, Math.round(v * scale / 2) * 2);
  return [even(width), even(height)];
}

// Accept only plain http(s) URLs; the board serves POST /frame and POST /control.
export function frameUrl(text, defaultPath = '/frame') {
  let url;
  try { url = new URL(String(text).trim()); } catch { return null; }
  if (url.protocol !== 'http:' && url.protocol !== 'https:') return null;
  if (url.username || url.password) return null;
  if (url.pathname === '/' || url.pathname === '') url.pathname = defaultPath;
  return url.href;
}

// Sends frames one at a time: a slow board drops the rate instead of queueing.
export async function runFrameLoop({grab, post, isRunning, onResult,
  interval = FRAME_INTERVAL_MS, sleep = ms => new Promise(r => setTimeout(r, ms)), now = Date.now}) {
  let sent = 0, failed = 0;
  while (isRunning()) {
    const started = now();
    try {
      const blob = await grab();
      if (!isRunning()) break;
      if (blob) { await post(blob); sent++; }
    } catch (error) {
      failed++;
      onResult?.({sent, failed, error});
      await sleep(interval * 2);
      continue;
    }
    onResult?.({sent, failed});
    await sleep(Math.max(0, interval - (now() - started)));
  }
  return {sent, failed};
}
