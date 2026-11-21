// Prints "<name>|<hex>" for each valid vector, encoded with the gateway's own
// encodeFrame, so CTL-04 can compare it with the shared vector file.
// Usage: node encode_vectors.mjs <gateway/controls.js> <control_frames.json>
import {readFileSync} from 'node:fs';
import {pathToFileURL} from 'node:url';
import {resolve} from 'node:path';

const [controlsPath, vectorsPath] = process.argv.slice(2);
const {encodeFrame} = await import(pathToFileURL(resolve(controlsPath)).href);
for (const v of JSON.parse(readFileSync(vectorsPath, 'utf8')).valid)
  console.log(v.name + '|' + Buffer.from(encodeFrame(v.sequence, v.axes)).toString('hex'));
