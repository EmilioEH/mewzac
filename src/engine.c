/* SPDX-License-Identifier: GPL-3.0-only */
/* MEWZAC's sound: the loop player, 8 synthesized drums, 13 synth voices (bass, chords, lead), a
 * tempo-synced ping-pong echo and a soft limiter. Integers only: the FM-1 has no FPU.
 *
 * Everything here runs in audio(), the audio interrupt. loop() talks to it only through engine_cmd()
 * (a ring of commands) and the song's recorded notes, which it appends and audio() reads. */
#include "groove.h"

#define CHUNK 16u                      /* envelopes, filters and the clock step every 16 frames */
#define THR (SAMPLE_RATE * 60u * 100u) /* clock: += bpm x 100 x PPQN a frame, a tick at THR */

volatile uint16_t eng_pos;
volatile uint8_t eng_frac, eng_play, eng_click;
volatile uint16_t eng_wraps;
static uint32_t acc;

/* ---- the scales: bit n = n semitones above the root */
static const uint16_t SCALE_MASK[SCALES] = {661, 1193, 2741, 1453, 1257, 1709, 4095};
const char *const SCALE_NAME[SCALES] = {"PENTA", "PENTA MIN", "MAJOR", "MINOR", "BLUES", "DORIAN", "ANY NOTE"};

static int in_scale(int p) {
  int rel = (p - song.root + 120) % 12;
  return SCALE_MASK[song.scale % SCALES] >> rel & 1;
}
static int snap(int p) { /* down to the nearest note of the scale (the root always is one) */
  for (int i = 0; i < 12 && !in_scale(p); i++) {
    p--;
  }
  return p;
}
static int scale_up(int p, int steps) {
  if (song.scale % SCALES == SCALES - 1) { /* any note: a major triad */
    return p + (steps == 2 ? 4 : steps == 4 ? 7 : steps);
  }
  for (int i = 0; steps > 0 && i < 48; i++) {
    p++;
    steps -= in_scale(p);
  }
  return p;
}
static const int8_t TRACK_BASE[TRACKS] = {0, -12, 0, 12};
static int pitch_of(int track, uint8_t note) {
  int p = 53 + note_key(note) + 12 * note_oct(note) + TRACK_BASE[track];
  p = snap(p);
  return p < 12 ? 12 : p > 115 ? 115 : p;
}

/* ---- small helpers */
static uint32_t noise_state = 0x9E3779B9u;
static inline int32_t noise(void) {
  noise_state ^= noise_state << 13;
  noise_state ^= noise_state >> 17;
  noise_state ^= noise_state << 5;
  return (int32_t)noise_state >> 17; /* +-16384 */
}
static int32_t k_of(int ms) { /* Q16 decay per chunk for a time constant of ms */
  if (ms < 1) {
    ms = 1;
  }
  return 65536 - 23777 / ms;
}
static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi
                                                                                            : v; }
static uint32_t inc_q8(int32_t n8) { /* phase step of a note in 1/256 semitones */
  int n = n8 >> 8, fr = n8 & 255;
  if (n < 0) {
    return note_inc(0);
  }
  if (n >= 127) {
    return note_inc(127);
  }
  uint32_t a = note_inc(n), b = note_inc(n + 1);
  return a + (((b - a) >> 8) * (uint32_t)fr);
}
static int32_t svf_f(int32_t n8) { /* the state-variable filter's f, Q15, for a cutoff note (kept stable) */
  n8 = clampi(n8, 12 << 8, 114 << 8);
  return 2 * sine(inc_q8(n8) >> 1);
}
typedef struct {
  int32_t low, band;
} svf_t;
static inline void svf(svf_t *s, int32_t in, int32_t f, int32_t q) { /* in +-8191; keeps low and band */
  int32_t low = s->low + ((f * s->band) >> 15);
  low = clampi(low, -65535, 65535);
  int32_t high = clampi(in - low - (((q >> 1) * s->band) >> 14), -65535, 65535);
  s->band = clampi(s->band + ((f * high) >> 15), -60000, 60000);
  s->low = low;
}

/* ---- the synth sounds */
enum { W_SAW, W_SQR, W_SINE, W_DUO, W_FM, W_ORGAN };
typedef struct {
  const char *name;
  uint8_t wave, sub;
  uint16_t a, d, s, r; /* attack, decay (time constants), sustain Q15, release; ms */
  uint8_t cut, res, env;
  uint16_t fd, glide;
  uint16_t ratio, index; /* FM: modulator ratio Q8, depth */
  uint8_t vib;
} patch_t;

#define NSOUND 4
static const patch_t PATCH[TRACKS][NSOUND] = {
  {{.name = "BOOM"}, {.name = "PUNCH"}, {.name = "TINY"}, {.name = "ROBOT"}}, /* drum kits: see the drums */
  {
    {"SUB", W_SINE, 110, 2, 400, 26000, 90, 100, 0, 0, 100, 0, 0, 0, 0},
    {"ACID", W_SAW, 0, 1, 250, 18000, 60, 46, 100, 50, 160, 60, 0, 0, 0},
    {"PLUCK", W_SQR, 60, 1, 220, 0, 90, 58, 30, 42, 120, 0, 0, 0, 0},
    {"FAT", W_DUO, 150, 3, 500, 24000, 140, 62, 25, 26, 300, 30, 0, 0, 0},
  },
  {
    {"PAD", W_DUO, 0, 350, 900, 26000, 900, 80, 10, 14, 900, 0, 0, 0, 0},
    {"KEYS", W_FM, 0, 1, 1100, 5000, 400, 114, 0, 0, 350, 0, 256, 22000, 0},
    {"ORGAN", W_ORGAN, 0, 4, 60, 30000, 70, 112, 0, 0, 100, 0, 0, 0, 0},
    {"GLASS", W_FM, 0, 3, 1400, 0, 700, 114, 0, 0, 900, 0, 896, 9000, 0},
  },
  {
    {"BEEP", W_SQR, 0, 1, 180, 20000, 80, 100, 0, 0, 100, 0, 0, 0, 10},
    {"SAW", W_SAW, 0, 3, 300, 22000, 150, 70, 50, 30, 260, 40, 0, 0, 22},
    {"BELL", W_FM, 0, 1, 900, 0, 600, 114, 0, 0, 700, 0, 896, 16000, 0},
    {"FLUTE", W_SINE, 0, 60, 200, 26000, 160, 100, 0, 0, 100, 0, 0, 0, 34},
  },
};
int sound_count(int track) { return (void)track, NSOUND; }
const char *sound_name(int track, int s) { return PATCH[track % TRACKS][s % NSOUND].name; }

/* ---- synth voices: 0 bass (one voice, glides), 1..8 chords, 9..12 lead */
#define NV 13
static const uint8_t POOL_LO[TRACKS] = {0, 0, 1, 9}, POOL_HI[TRACKS] = {0, 1, 9, 13};
typedef struct {
  uint8_t on; /* 0 free, 1 held, 2 released */
  uint8_t track, note, src, stage, vel, pan, pitch;
  uint16_t off; /* a recorded note: ticks left */
  uint32_t age, ph1, ph2, ph3, inc, target;
  int32_t lvl, flv, amp; /* amplitude envelope Q30, filter envelope Q30, the level now Q15 */
  int32_t astep, dk, rk, fk, gk;
  svf_t f;
} voice_t;
static voice_t vc[NV];
static uint32_t age_n;

/* ---- drums: one voice each */
enum { D_KICK, D_SNARE, D_CLAP, D_CHH, D_OHH, D_TOML, D_TOMH, D_COW };
const char *const DRUM_NAME[8] = {"KICK", "SNARE", "CLAP", "HAT", "OPEN HAT", "LOW TOM", "HIGH TOM", "COWBELL"};
static const uint8_t KEY_DRUM[KEY_COUNT] = {0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7};
static const int8_t KEY_VAR[KEY_COUNT] = {0, 2, -2, 4, 0, 2, -2, 0, 2, -2, 0, 2, -2, 4, 0, 2, -2, 0, 2, -2, 0, 2, -2, 4, 0, 2, -2};
static const uint8_t GM_DRUM[8] = {36, 38, 39, 42, 46, 45, 50, 56};
int drum_of_key(int k) { return KEY_DRUM[k % KEY_COUNT]; }
typedef struct {
  uint8_t on, key, vel;
  uint32_t t; /* chunks since the hit */
  int32_t amp, ak, amp2, ak2, pen, pk, a_now, a2_now;
  uint32_t ph, ph2, inc, inc2, inc0;
  int32_t lp;
  svf_t f;
  int32_t ff, fq;
} drum_t;
static drum_t dr[8];

/* the drum kits: pitch (semitones), decay (%), and the TINY one's crusher */
static const int8_t KIT_PITCH[NSOUND] = {0, 3, 12, -5};
static const uint8_t KIT_DECAY[NSOUND] = {130, 70, 55, 100};

/* ---- the echo: a dotted 8th, ping-pong. 60 BPM at the longest */
#define DMAX 33100
static int16_t dl[2][DMAX] POOL;
static uint32_t dw, dtime;
static int32_t dlp[2];

/* the metronome */
static int32_t click_amp;
static uint32_t click_ph, click_inc;

/* ---- the command ring from loop() */
#define QN 64u
static struct {
  uint8_t cmd, track, note, vel;
} q[QN];
static volatile uint8_t qw, qr;

void engine_cmd(uint8_t cmd, uint8_t track, uint8_t note, uint8_t vel) {
  uint8_t w = qw;
  if ((uint8_t)(w - qr) >= QN) {
    return;
  }
  q[w % QN].cmd = cmd, q[w % QN].track = track, q[w % QN].note = note, q[w % QN].vel = vel;
  BARRIER();
  qw = (uint8_t)(w + 1);
}

void engine_init(void) {
  for (uint32_t i = 0; i < DMAX; i++) {
    dl[0][i] = dl[1][i] = 0;
  }
}

/* ---- notes */
static void voice_start(int v, int track, int pitch, uint8_t note, uint8_t vel, uint8_t src, uint16_t len, uint8_t pan) {
  const patch_t *p = &PATCH[track][song.trk[track].sound % NSOUND];
  voice_t *x = &vc[v];
  int legato = p->glide && x->on == 1 && x->track == track;
  x->target = note_inc(pitch);
  if (!legato) {
    x->inc = x->target;
    x->stage = 0;
    x->flv = 1 << 30;
    if (x->on == 0) {
      x->lvl = 0, x->amp = 0, x->f.low = x->f.band = 0;
    }
  }
  x->on = 1, x->track = (uint8_t)track, x->note = note, x->src = src, x->vel = vel, x->off = len, x->pan = pan;
  x->pitch = (uint8_t)pitch;
  x->age = ++age_n;
  x->astep = p->a ? 389600000 / p->a : 1 << 30;
  x->dk = k_of(p->d), x->rk = k_of(p->r), x->fk = k_of(p->fd), x->gk = 65536 - k_of(p->glide ? p->glide : 1);
  midi_send((uint8_t)(0x90 | track), (uint8_t)pitch, vel);
}

static int voice_pick(int track) {
  int lo = POOL_LO[track], hi = POOL_HI[track], best = lo;
  for (int v = lo; v < hi; v++) {
    if (!vc[v].on) {
      return v;
    }
  }
  for (int v = lo; v < hi; v++) { /* the oldest released, else the oldest */
    int a = vc[v].on == 2, b = vc[best].on == 2;
    if (a > b || (a == b && vc[v].age < vc[best].age)) {
      best = v;
    }
  }
  return best;
}

static void voice_release(voice_t *x) {
  if (x->on == 1) {
    x->on = 2, x->stage = 2;
    midi_send((uint8_t)(0x80 | x->track), x->pitch, 0);
  }
}

static void drum_hit(int key, uint8_t vel) {
  int d = KEY_DRUM[key % KEY_COUNT], kit = song.trk[T_DRUMS].sound % NSOUND;
  const uint8_t *kn = song.trk[T_DRUMS].knob;
  int pitch = KIT_PITCH[kit] + KEY_VAR[key % KEY_COUNT] + ((kn[0] - 64) * 12) / 64;
  int decay = KIT_DECAY[kit] * (kn[1] + 32) / 96; /* % */
  drum_t *x = &dr[d];
  x->on = 1, x->key = (uint8_t)key, x->vel = vel, x->t = 0;
  x->amp = 65536, x->amp2 = 65536, x->pen = 65536;
  x->f.low = x->f.band = 0, x->lp = 0;
  switch (d) {
    case D_KICK:
      x->inc = note_inc(29 + pitch), x->inc0 = note_inc(62 + pitch), x->ph = 0;
      x->ak = k_of(180 * decay / 100), x->pk = k_of(22);
      break;
    case D_SNARE:
      x->inc = note_inc(55 + pitch), x->ak = k_of(35 * decay / 100), x->ak2 = k_of(70 * decay / 100);
      x->ff = svf_f((100 + pitch) << 8), x->fq = 40000;
      break;
    case D_CLAP:
      x->ak = k_of(3), x->ak2 = k_of(110 * decay / 100), x->amp2 = 0;
      x->ff = svf_f((88 + pitch) << 8), x->fq = 22000;
      break;
    case D_CHH:
      x->ak = k_of(18 * decay / 100);
      dr[D_OHH].ak = k_of(4); /* the closed hat chokes the open one */
      break;
    case D_OHH:
      x->ak = k_of(160 * decay / 100);
      break;
    case D_TOML:
    case D_TOMH:
      x->inc = note_inc((d == D_TOML ? 43 : 50) + pitch), x->inc0 = note_inc((d == D_TOML ? 50 : 57) + pitch);
      x->ak = k_of(150 * decay / 100), x->pk = k_of(40), x->ph = 0;
      break;
    default: /* cowbell */
      x->inc = note_inc(74 + pitch), x->inc2 = note_inc(80 + pitch), x->ak = k_of(70 * decay / 100);
      x->ff = svf_f((79 + pitch) << 8), x->fq = 30000;
      break;
  }
  x->ak = clampi(x->ak, 0, 65535);
  midi_send(0x99, GM_DRUM[d], vel);
  midi_send(0x89, GM_DRUM[d], 0);
}

static void note_on(int track, uint8_t note, uint8_t vel, uint8_t src, uint16_t len) {
  if (track == T_DRUMS) {
    drum_hit(note_key(note), vel);
    return;
  }
  int p = pitch_of(track, note);
  if (track == T_CHORDS) {
    int n[3] = {p, scale_up(p, 2), scale_up(p, 4)};
    for (int i = 0; i < 3; i++) {
      voice_start(voice_pick(track), track, n[i] > 120 ? n[i] - 12 : n[i], note, vel, src, len, (uint8_t)(128 + (i - 1) * 50));
    }
    return;
  }
  voice_start(voice_pick(track), track, p, note, vel, src, len, track == T_LEAD ? 140 : 128);
}

static void note_off(int track, uint8_t note, uint8_t src) {
  for (int v = 0; v < NV; v++) {
    if (vc[v].on == 1 && vc[v].track == track && vc[v].note == note && vc[v].src == src) {
      voice_release(&vc[v]);
    }
  }
}

static void track_off(int track, int src_only) {
  for (int v = 0; v < NV; v++) {
    if (vc[v].on == 1 && (track < 0 || vc[v].track == track) && (!src_only || vc[v].src == 1)) {
      voice_release(&vc[v]);
    }
  }
}

/* ---- the loop player: one clock tick */
static void tick(void) {
  uint16_t L = loop_len();
  uint16_t pos = (uint16_t)(eng_pos + 1);
  if (pos >= L) {
    pos = 0;
    eng_wraps++;
  }
  eng_pos = pos;
  for (int v = 0; v < NV; v++) {
    if (vc[v].on == 1 && vc[v].src == 1 && vc[v].off && --vc[v].off == 0) {
      voice_release(&vc[v]);
    }
  }
  if (eng_click && pos % PPQN == 0) {
    click_amp = 65536, click_ph = 0, click_inc = note_inc(pos == 0 ? 96 : 89);
  }
  for (int t = 0; t < TRACKS; t++) {
    if (song.trk[t].mute) {
      continue;
    }
    uint16_t n = song.trk[t].count;
    event_t *e = song.ev[t];
    for (uint16_t i = 0; i < n && i < MAX_EV; i++) {
      if (e[i].tick != pos) {
        continue;
      }
      if (e[i].vel & 0x80) {
        e[i].vel &= 0x7F;
        continue;
      }
      if (e[i].len) {
        note_on(t, e[i].note, e[i].vel, 1, e[i].len);
      }
    }
  }
}

static void commands(void) {
  while (qr != qw) {
    uint8_t r = qr;
    BARRIER();
    uint8_t c = q[r % QN].cmd, t = q[r % QN].track % TRACKS, n = q[r % QN].note, v = q[r % QN].vel;
    qr = (uint8_t)(r + 1);
    switch (c) {
      case CMD_ON:
        note_on(t, n, v, 0, 0);
        break;
      case CMD_OFF:
        note_off(t, n, 0);
        break;
      case CMD_PLAY:
        eng_pos = (uint16_t)(loop_len() - 1), acc = THR - 1, eng_play = 1;
        break;
      case CMD_STOP:
        eng_play = 0;
        track_off(-1, 1);
        break;
      case CMD_TRACK_OFF:
        track_off(t, 1);
        break;
    }
  }
}

/* ---- per chunk: the envelopes */
static void voice_control(voice_t *x) {
  const patch_t *p = &PATCH[x->track][song.trk[x->track].sound % NSOUND];
  if (x->stage == 0) {
    if (x->lvl >= (1 << 30) - x->astep) {
      x->lvl = 1 << 30, x->stage = 1;
    } else {
      x->lvl += x->astep;
    }
  } else if (x->stage == 1) {
    int32_t s = (int32_t)p->s << 15;
    x->lvl = s + (int32_t)(((int64_t)(x->lvl - s) * x->dk) >> 16);
  } else {
    x->lvl = (int32_t)(((int64_t)x->lvl * x->rk) >> 16);
    if (x->lvl < 1 << 17) {
      x->on = 0, x->lvl = 0;
    }
  }
  x->flv = (int32_t)(((int64_t)x->flv * x->fk) >> 16);
  if (x->inc != x->target) {
    int32_t d = (int32_t)(x->target - x->inc);
    int32_t step = (int32_t)(((int64_t)d * x->gk) >> 16);
    x->inc = step ? x->inc + (uint32_t)step : x->target;
  }
}

/* ---- the sound */
static void render_voice(voice_t *x, int32_t *L, int32_t *R, int32_t *send, uint32_t n, int32_t lfo) {
  const patch_t *p = &PATCH[x->track][song.trk[x->track].sound % NSOUND];
  const uint8_t *kn = song.trk[x->track].knob;
  int32_t a0 = x->amp;
  voice_control(x);
  /* the level at the end of this chunk, Q15: envelope x velocity x LEVEL x the track's mix */
  static const int32_t MIX[TRACKS] = {0, 30000, 12000, 18000};
  int32_t a1 = (int32_t)(((int64_t)(x->lvl >> 15) * x->vel * kn[3] * MIX[x->track]) >> 28);
  x->amp = a1;
  /* the filter: TONE moves the cutoff, SHAPE the resonance and the envelope's sweep */
  int shape = kn[1];
  int32_t res = clampi(p->res + (shape - 64), 0, 120);
  int32_t envamt = p->env * (shape + 16) / 80;
  int32_t c8 = ((int32_t)p->cut + ((kn[0] - 64) * 3) / 4) * 256 + envamt * (x->flv >> 22) + (x->pitch - 60) * 128;
  int32_t f = svf_f(c8), qd = 65536 - res * 485;
  int filt = c8 < (114 << 8) || res > 0;
  uint32_t inc = x->inc;
  if (p->vib) {
    int32_t d = ((int32_t)(inc >> 12) * (sine((uint32_t)lfo) >> 7)) >> 8; /* +-inc/4096 */
    inc += (uint32_t)(d * p->vib * 5 / 2);
  }
  uint32_t inc2 = inc + (inc >> 7), inc3 = inc >> 1;
  int32_t idx = (int32_t)(((int64_t)p->index * (x->flv >> 15)) >> 15);
  uint32_t mi = (uint32_t)p->ratio * (inc >> 8);
  uint32_t ph1 = x->ph1, ph2 = x->ph2, ph3 = x->ph3;
  int32_t pl = 256 - x->pan, pr = x->pan;
  int32_t sendk = song.trk[x->track].knob[2];
  for (uint32_t i = 0; i < n; i++) {
    int32_t s;
    ph1 += inc;
    switch (p->wave) {
      case W_SAW:
        s = ((int32_t)(ph1 >> 16) - 32768) * 3 / 8;
        break;
      case W_SQR:
        s = (ph1 & 0x80000000u) ? 9000 : -9000;
        break;
      case W_SINE:
        s = sine(ph1) * 3 / 8;
        break;
      case W_DUO:
        ph2 += inc2;
        s = (((int32_t)(ph1 >> 16) - 32768) + ((int32_t)(ph2 >> 16) - 32768)) * 3 / 16;
        break;
      case W_FM:
        ph2 += mi;
        s = sine(ph1 + ((uint32_t)(sine(ph2) * idx) << 1)) * 3 / 8;
        break;
      default: /* organ */
        s = ((sine(ph1) + (sine(ph1 << 1) >> 1) + (sine(ph1 << 2) >> 2)) * 219) >> 10;
        break;
    }
    if (p->sub) {
      ph3 += inc3;
      s += ((ph3 & 0x80000000u) ? 40 : -40) * p->sub;
    }
    if (filt) {
      svf(&x->f, s >> 2, f, qd);
      s = x->f.low << 2;
    }
    int32_t a = a0 + (((a1 - a0) * (int32_t)i) >> 4);
    s = (s * a) >> 15;
    L[i] += (s * pl) >> 8;
    R[i] += (s * pr) >> 8;
    send[i] += (s * sendk) >> 7;
  }
  x->ph1 = ph1, x->ph2 = ph2, x->ph3 = ph3;
}

static void render_drum(int d, drum_t *x, int32_t *L, int32_t *R, int32_t *send, uint32_t n) {
  int32_t a0 = x->a_now, b0 = x->a2_now;
  x->amp = (int32_t)(((int64_t)x->amp * x->ak) >> 16);
  x->amp2 = (int32_t)(((int64_t)x->amp2 * x->ak2) >> 16);
  x->pen = (int32_t)(((int64_t)x->pen * x->pk) >> 16);
  x->t++;
  if (d == D_CLAP && (x->t == 28 || x->t == 56)) {
    x->amp = 65536;
  }
  if (d == D_CLAP && x->t == 84) {
    x->amp2 = 65536;
  }
  int32_t a1 = x->amp, b1 = x->amp2;
  x->a_now = a1, x->a2_now = b1;
  if (a1 < 40 && b1 < 40 && x->t > 90) {
    x->on = 0;
  }
  if (d == D_KICK || d == D_TOML || d == D_TOMH) {
    x->ph2 = x->inc + (uint32_t)(((int64_t)(x->inc0 - x->inc) * x->pen) >> 16);
  }
  const uint8_t *kn = song.trk[T_DRUMS].knob;
  int32_t gain = (x->vel * kn[3]) >> 6; /* ~0..250 */
  int32_t sendk = kn[2];
  for (uint32_t i = 0; i < n; i++) {
    int32_t a = (a0 + (((a1 - a0) * (int32_t)i) >> 4)) >> 1, b = (b0 + (((b1 - b0) * (int32_t)i) >> 4)) >> 1; /* Q15 */
    int32_t s = 0;
    switch (d) {
      case D_KICK:
        x->ph += x->ph2;
        s = (sine(x->ph) * a) >> 15;
        s = s > 20000 ? 20000 + ((s - 20000) >> 2) : s;
        if (x->t < 2) {
          s += noise() >> 2;
        }
        break;
      case D_SNARE:
        x->ph += x->inc;
        svf(&x->f, noise() >> 1, x->ff, x->fq);
        s = ((sine(x->ph) * a) >> 16) + ((x->f.band * b) >> 14);
        break;
      case D_CLAP: {
        svf(&x->f, noise() >> 1, x->ff, x->fq);
        s = (x->f.band * (a + b)) >> 13;
        break;
      }
      case D_CHH:
      case D_OHH: {
        int32_t w = noise();
        x->lp += (w - x->lp) >> 1;
        s = ((w - x->lp) * a) >> 15;
        s = s * 3 / 4;
        break;
      }
      case D_TOML:
      case D_TOMH:
        x->ph += x->ph2;
        s = (sine(x->ph) * a) >> 16;
        break;
      default:
        x->ph += x->inc, x->ph2 += x->inc2;
        svf(&x->f, (((x->ph >> 31) ? 3000 : -3000) + ((x->ph2 >> 31) ? 3000 : -3000)), x->ff, x->fq);
        s = (x->f.band * a) >> 13;
        break;
    }
    s = (s * gain) >> 8;
    L[i] += s;
    R[i] += s;
    send[i] += (s * sendk) >> 8;
  }
}

static int32_t soft(int32_t x) { /* a gentle limiter: nothing gets loud enough to hurt */
  int32_t ax = x < 0 ? -x : x;
  if (ax > 14000) {
    ax = 14000 + ((ax - 14000) >> 2);
  }
  if (ax > 21000) {
    ax = 21000;
  }
  return x < 0 ? -ax : ax;
}

void audio(int32_t *out, uint32_t frames) {
  static int32_t lfo;
  static uint32_t crush_hold;
  static int32_t crush_l, crush_r;
  commands();
  uint32_t bpm = song.bpm < BPM_MIN ? BPM_MIN : song.bpm > BPM_MAX ? BPM_MAX
                                                                  : song.bpm;
  uint32_t want = 198450000u / (bpm * 100u);
  if (want != dtime) {
    dtime = want < DMAX - 1 ? want : DMAX - 1;
  }
  int kit = song.trk[T_DRUMS].sound % NSOUND;
  for (uint32_t c = 0; c < frames; c += CHUNK) {
    uint32_t n = frames - c < CHUNK ? frames - c : CHUNK;
    if (eng_play) {
      acc += bpm * 100u * PPQN * n;
      while (acc >= THR) {
        acc -= THR;
        tick();
      }
    }
    lfo += 8571000 * 2;
    int32_t L[CHUNK] = {0}, R[CHUNK] = {0}, S[CHUNK] = {0}, DL[CHUNK] = {0}, DR[CHUNK] = {0};
    for (int v = 0; v < NV; v++) {
      if (vc[v].on) {
        render_voice(&vc[v], L, R, S, n, lfo);
      }
    }
    for (int d = 0; d < 8; d++) {
      if (dr[d].on) {
        render_drum(d, &dr[d], DL, DR, S, n);
      }
    }
    for (uint32_t i = 0; i < n; i++) {
      if (kit == 2) { /* TINY: a little crunchy */
        if (crush_hold++ % 3 == 0) {
          crush_l = DL[i], crush_r = DR[i];
        }
        DL[i] = crush_l & ~0x1FF, DR[i] = crush_r & ~0x1FF;
      }
      if (kit == 3 && (i & 1)) { /* ROBOT: half the sample rate */
        DL[i] = DL[i - 1], DR[i] = DR[i - 1];
      }
      L[i] += DL[i], R[i] += DR[i];
    }
    /* the echo */
    for (uint32_t i = 0; i < n; i++) {
      uint32_t rd = dw >= dtime ? dw - dtime : dw + DMAX - dtime;
      int32_t el = dl[0][rd], er = dl[1][rd];
      int32_t inl = S[i] / 2 + ((er * 15) >> 5), inr = (el * 15) >> 5;
      dlp[0] += (inl - dlp[0]) >> 1, dlp[1] += (inr - dlp[1]) >> 1;
      dl[0][dw] = (int16_t)clampi(dlp[0], -32767, 32767);
      dl[1][dw] = (int16_t)clampi(dlp[1], -32767, 32767);
      dw = dw + 1 < DMAX ? dw + 1 : 0;
      L[i] += el, R[i] += er;
      if (click_amp > 64) {
        click_ph += click_inc;
        int32_t s = (sine(click_ph) * (click_amp >> 1)) >> 18;
        L[i] += s, R[i] += s;
        click_amp -= click_amp >> 8;
      }
      out[2 * (c + i)] += soft(L[i]);
      out[2 * (c + i) + 1] += soft(R[i]);
    }
  }
  eng_frac = (uint8_t)(acc / (THR >> 8));
}

uint32_t engine_lit(int track) {
  uint32_t m = 0;
  if (track == T_DRUMS) {
    for (int d = 0; d < 8; d++) {
      if (dr[d].on && dr[d].t < 300) {
        m |= 1u << dr[d].key;
      }
    }
    return m;
  }
  for (int v = 0; v < NV; v++) {
    if (vc[v].on == 1 && vc[v].track == track) {
      m |= 1u << note_key(vc[v].note);
    }
  }
  return m;
}

int engine_busy(void) {
  for (int v = 0; v < NV; v++) {
    if (vc[v].on) {
      return 1;
    }
  }
  for (int d = 0; d < 8; d++) {
    if (dr[d].on) {
      return 1;
    }
  }
  return 0;
}
