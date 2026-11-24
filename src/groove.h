/* SPDX-License-Identifier: GPL-3.0-only */
/* MEWZAC: a four-track groovebox for the M-VAVE FM-1 that anyone can play.
 * What the sound engine (engine.c) and the panel (main.c) share. */
#ifndef GROOVE_H
#define GROOVE_H
#include "fm1.h"

#define BARRIER() __asm__ volatile("" ::: "memory")

#define TRACKS 4
enum { T_DRUMS, T_BASS, T_CHORDS, T_LEAD };

#define PPQN 24             /* clock ticks per beat */
#define STEP 6              /* a 16th note, in ticks */
#define BAR (4 * PPQN)      /* ticks per bar */
#define MAX_EV 440          /* recorded notes per track */
#define BPM_MIN 60
#define BPM_MAX 180
#define SCALES 7

/* a recorded note. note: the key (0..26) and the octave it was played in, (oct + 2) * 32 + key, so a
 * song follows the scale and key it is played in. len 0: the key is still held (it does not play yet).
 * vel bit 7: skip it once (it was played live a moment before the playhead got there) */
typedef struct {
  uint16_t tick, len;
  uint8_t note, vel;
} event_t;

typedef struct {
  uint8_t sound, mute;
  int8_t oct;
  uint8_t knob[4]; /* TONE, SHAPE, ECHO, LEVEL: 0..127 */
  uint8_t pad;
  uint16_t count;
} track_t;

#define SONG_MAGIC 0x4D575A31u /* "MWZ1" */
typedef struct {
  uint32_t magic;
  uint16_t bpm;
  uint8_t bars, root, scale, quant;
  uint8_t pad[2];
  track_t trk[TRACKS];
  event_t ev[TRACKS][MAX_EV];
} song_t;

extern song_t song;

static inline uint8_t pack_note(int key, int oct) { return (uint8_t)((oct + 2) * 32 + key); }
static inline int note_key(uint8_t n) { return n & 31; }
static inline int note_oct(uint8_t n) { return (n >> 5) - 2; }

/* ---- engine.c */
enum { CMD_ON, CMD_OFF, CMD_PLAY, CMD_STOP, CMD_TRACK_OFF };
void engine_init(void);
void engine_cmd(uint8_t cmd, uint8_t track, uint8_t note, uint8_t vel); /* from loop() */
uint32_t engine_lit(int track);                                         /* keys sounding on a track */
int engine_busy(void);                                                  /* anything still sounding */
int sound_count(int track);
const char *sound_name(int track, int sound);
int drum_of_key(int key);

extern volatile uint16_t eng_pos;   /* the playhead, in ticks */
extern volatile uint8_t eng_frac;   /* and how far into the tick, /256 */
extern volatile uint8_t eng_play;   /* the loop runs */
extern volatile uint8_t eng_click;  /* metronome */
extern volatile uint16_t eng_wraps; /* times the loop came round */

extern const char *const SCALE_NAME[SCALES];
extern const char *const DRUM_NAME[8];

static inline uint16_t loop_len(void) { return (uint16_t)(song.bars * BAR); }

#endif
