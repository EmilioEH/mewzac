// SPDX-License-Identifier: GPL-3.0-only
// MEWZAC without an FM-1 or a browser: runs build/web/firmware.wasm in node, plays a scripted
// performance on the panel, and writes the sound (WAV) and the screen (BMP) to build/sim/.
//
//   node tools/sim.mjs            the demo script below
//
// It also reports how much of real time the firmware's audio() took in WebAssembly (a rough
// guide only: the FM-1's CPU is not this one).
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs'

const wasm = new WebAssembly.Instance(new WebAssembly.Module(readFileSync('build/web/firmware.wasm')), {})
const x = wasm.exports
const mem = () => new Uint8Array(x.memory.buffer)
const OUT = 'build/sim'
mkdirSync(OUT, { recursive: true })

const BTN = { 'OCT-': 0, 'OCT+': 1, FX: 2, SEL: 3, ENV: 4, LFO: 5, EDIT: 6, GLO: 7, HOME: 8, SAVE: 9, ARP: 10, SEQ: 11, PLAY: 12, REC: 13 }
const ENC = { SELECT: 0, ALGORITHM: 1, KNOB1: 2, KNOB2: 3, KNOB3: 4, KNOB4: 5, PRESETS: 6 }

let keyMask = 0
let btnMask = 0
const events = [] // [ms, fn]
const at = (ms, fn) => events.push([ms, fn])
const key = (ms, k, dur = 120) => {
  at(ms, () => x.web_keys((keyMask |= 1 << k)))
  at(ms + dur, () => x.web_keys((keyMask &= ~(1 << k))))
}
const button = (ms, b, dur = 60) => {
  at(ms, () => x.web_buttons((btnMask |= 1 << BTN[b])))
  at(ms + dur, () => x.web_buttons((btnMask &= ~(1 << BTN[b]))))
}
const turn = (ms, e, n) => at(ms, () => x.web_encoder(ENC[e], n))
const shots = []
const shot = (ms, name) => shots.push([ms, name])

// ---- the demo: 100 BPM, 2 bars = 4.8 s a loop. A beat is 600 ms, a 16th 150 ms
const BEAT = 600
const LOOP = 8 * BEAT
let t = 500
shot(400, '0-start')
button(t, 'REC') // the loop starts, recording DRUMS
const t0 = t + 30
for (let b = 0; b < 8; b++) {
  key(t0 + b * BEAT + 12, b % 2 ? 4 : 0, 80) // kick, snare
  key(t0 + b * BEAT + BEAT / 2 - 8, 11, 60) // hats on the off-beat
}
shot(t0 + LOOP - 100, '1-drums')
// pass 2: switch to BASS while recording, a little riff
t = t0 + LOOP
button(t - 200, 'SEL')
const riff = [0, 0, 7, 5, 0, 0, 10, 12]
riff.forEach((k, i) => key(t + i * BEAT + 5, k, BEAT / 2))
// pass 3: CHORDS, one key a bar
t += LOOP
button(t - 200, 'ENV')
;[0, 5].forEach((k, i) => key(t + i * 4 * BEAT + 10, k, 4 * BEAT - 200))
// pass 4: LEAD
t += LOOP
button(t - 200, 'LFO')
;[12, 14, 16, 19, 21, 19, 16, 14].forEach((k, i) => key(t + i * BEAT + 300, k, 250))
shot(t + LOOP - 100, '2-four-tracks')
// stop recording, listen, then change the mood: minor, and ECHO up
t += LOOP
button(t, 'REC')
button(t + 2 * BEAT, 'EDIT') // PENTA MIN
for (let i = 0; i < 10; i++) turn(t + 2 * BEAT + i * 100, 'KNOB3', 1)
shot(t + 2 * BEAT + 200, '3-minor')
// undo the lead's take
button(t + LOOP, 'HOME')
shot(t + LOOP + 300, '4-undo')
t += 2 * LOOP
button(t, 'PLAY')
const END = t + 1500

events.sort((a, b) => a[0] - b[0])
x.web_boot()
const sr = 44100
const frames = Math.round((END * sr) / 1000)
const L = new Float32Array(frames)
const R = new Float32Array(frames)
let done = 0
let ei = 0
let si = 0
let cpu = 0
while (done < frames) {
  const ms = (done * 1000) / sr
  while (ei < events.length && events[ei][0] <= ms) events[ei++][1]()
  while (si < shots.length && shots[si][0] <= ms) writeShot(shots[si++][1])
  const n = Math.min(441, frames - done)
  const c0 = performance.now()
  x.web_render(n)
  cpu += performance.now() - c0
  const l = new Float32Array(x.memory.buffer, x.web_out_l(), n)
  const r = new Float32Array(x.memory.buffer, x.web_out_r(), n)
  L.set(l, done)
  R.set(r, done)
  done += n
}

function writeShot(name) {
  const fb = new Uint16Array(x.memory.buffer, x.web_fb(), 240 * 240)
  const row = 240 * 3
  const buf = Buffer.alloc(54 + row * 240)
  buf.write('BM', 0)
  buf.writeUInt32LE(buf.length, 2)
  buf.writeUInt32LE(54, 10)
  buf.writeUInt32LE(40, 14)
  buf.writeInt32LE(240, 18)
  buf.writeInt32LE(-240, 22)
  buf.writeUInt16LE(1, 26)
  buf.writeUInt16LE(24, 28)
  for (let i = 0; i < 240 * 240; i++) {
    const p = ((fb[i] & 0xff) << 8) | (fb[i] >> 8) // stored byte-swapped, as the panel takes it
    const o = 54 + i * 3
    buf[o] = ((p & 31) * 255) / 31
    buf[o + 1] = (((p >> 5) & 63) * 255) / 63
    buf[o + 2] = ((p >> 11) * 255) / 31
  }
  writeFileSync(`${OUT}/${name}.bmp`, buf)
}

// the sound, 16-bit stereo
const wav = Buffer.alloc(44 + frames * 4)
wav.write('RIFF', 0)
wav.writeUInt32LE(36 + frames * 4, 4)
wav.write('WAVEfmt ', 8)
wav.writeUInt32LE(16, 16)
wav.writeUInt16LE(1, 20)
wav.writeUInt16LE(2, 22)
wav.writeUInt32LE(sr, 24)
wav.writeUInt32LE(sr * 4, 28)
wav.writeUInt16LE(4, 32)
wav.writeUInt16LE(16, 34)
wav.write('data', 36)
wav.writeUInt32LE(frames * 4, 40)
let peak = 0
let clipped = 0
for (let i = 0; i < frames; i++) {
  for (const [c, a] of [
    [0, L],
    [1, R]
  ]) {
    const v = Math.max(-1, Math.min(1, a[i] * 2)) // the page's 6 dB back
    peak = Math.max(peak, Math.abs(v))
    if (Math.abs(v) >= 0.999) clipped++
    wav.writeInt16LE(Math.round(v * 32767), 44 + i * 4 + c * 2)
  }
}
writeFileSync(`${OUT}/demo.wav`, wav)
const log = x.web_log()
console.log(`${(frames / sr).toFixed(1)} s of sound in ${(cpu / 1000).toFixed(2)} s (${((100 * cpu) / ((frames / sr) * 1000)).toFixed(1)} % of real time, wasm)`)
console.log(`peak ${peak.toFixed(3)}, clipped samples ${clipped}, ${shots.length} screens in ${OUT}/`)
void log
