# MEWZAC

A four-track groovebox firmware for the **M-VAVE FM-1**, built to be easy enough for a three-year-old to play
and record songs on. Press keys and it makes music. Press **REC** and it records what you play, then loops it.

It runs on [foma](https://github.com/konsumer/foma) (Felucca's open platform), so there's also a browser emulator
of the FM-1 for trying it without the device.

## Playing it

| Control | What it does |
| --- | --- |
| **Keys** | Play the selected track. Keys snap to the scale, so nothing sounds wrong. |
| **FX / SEL / ENV / LFO** | Pick the track: **DRUMS**, **BASS**, **CHORDS** (one key plays a whole chord), **LEAD**. |
| **PLAY** | Start or stop the loop. |
| **REC** | Record onto the selected track (starts the loop if it's stopped). The loop keeps going, so each pass adds to it. Switch tracks while recording to build a song up. A metronome clicks while you record. |
| **HOME** | Undo the last pass of recording. **Hold** it to clear the selected track. |
| **SEQ** | Mute or unmute the selected track. |
| **SAVE** | **Hold** it to save the song. It loads again at power-on. |
| **OCT− / OCT+** | Shift the track down or up an octave. |
| **EDIT** | Change the scale: PENTA, PENTA MIN, MAJOR, MINOR, BLUES, DORIAN, ANY NOTE. Recorded notes follow the change, so one press turns a happy song into a sad one. |
| **ARP** | Change the key (C, C#, D, …). Recorded notes follow this too. |
| **GLO** | Snap to the beat (on by default, light on) or free timing. |
| **SELECT** | Tempo, 60–180 BPM. |
| **PRESETS** | Loop length: 1, 2, 4 or 8 bars. |
| **ALGORITHM** | The track's sound (see below). |
| **KNOB1–4** | **TONE** (on drums: **PITCH**), **SHAPE** (squelch and resonance; on drums: **DECAY**), **ECHO**, **LEVEL**. |
| **MASTER** | Volume. The firmware's limiter keeps the output below a safe level whatever the knob is set to. |

The keys light up as the selected track plays.

### The sounds

- **DRUMS:** kits BOOM, PUNCH, TINY (crunchy) and ROBOT. The keyboard runs, low to high, through kick, snare,
  clap, hat, open hat, low tom, high tom and cowbell. Each sound covers 3–4 keys at slightly different pitches.
- **BASS** (one note at a time, with glide): SUB, ACID, PLUCK, FAT.
- **CHORDS** (one key plays three notes from the scale): PAD, KEYS, ORGAN, GLASS.
- **LEAD:** BEEP, SAW, BELL, FLUTE.

Everything is synthesized with integer DSP, since the FM-1 has no FPU. There are no samples. The engine has
13 synth voices with state-variable filters, 8 drum voices, a tempo-synced ping-pong echo and a soft limiter.
Notes also go out over USB MIDI: drums on channel 10, the other tracks on channels 2–4.

## Developing

```sh
tools/emu.sh serve     # build the emulator with your own clang and serve it at http://localhost:8000
node tools/sim.mjs     # play a scripted song headless: build/sim/demo.wav and screenshots
./fm1 dev              # the full foma setup in Docker: emulator, installer and the FM-1 package
./fm1 build            # build/mewzac-VERSION.fwsc
```

- `src/main.c`: the panel, the screen, recording, undo and saving.
- `src/engine.c`: the sound (voices, drums, echo) and the loop player.
- `src/groove.h`: the song format the two share.

The platform API is [platform/fm1.h](platform/fm1.h). Read the chip's rules (no floats, keep `audio()` short) in
[foma's README](https://github.com/konsumer/foma#the-rules-of-the-chip).

CI (`.github/workflows/pages.yml`) builds the FM-1 package with its safety checks on every push and attaches the
`.fwsc` to the run. On `main` it also publishes the emulator to GitHub Pages.

## Installing on the FM-1

Back up your projects first. Installing firmware is at your own risk.

- Install from the page's **Install** tab in Chrome or Edge, or run `./fm1 flash`.
- To get back to the stock firmware, use the **Rescue** tab or the
  [FM-1 Firmware Hub](https://thefirmwarehub.com).
- If the FM-1 doesn't start, hold **OCT-** while switching it on. That's the USB rescue mode; install again from
  there.

The package identity is `FM-1_74x`. Check the hub before sharing a build, to make sure no other firmware uses it.

## License

GPL-3.0-only. Credits and third-party parts are in [LICENSING.md](LICENSING.md).
