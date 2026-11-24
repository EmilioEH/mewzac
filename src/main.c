/* SPDX-License-Identifier: GPL-3.0-only */
/* MEWZAC: a four-track groovebox for the M-VAVE FM-1 that anyone can play, a three-year-old included.
 *
 *   KEYS        play the selected track. Every key is in the scale, so nothing sounds wrong
 *   FX SEL ENV LFO   pick the track: DRUMS, BASS, CHORDS (one key = a whole chord), LEAD
 *   PLAY        start / stop the loop
 *   REC         record onto the selected track while the loop goes round (it starts the loop too).
 *               Notes snap to the beat (GLO turns that off). Switch tracks and keep recording
 *   HOME        undo the last pass of recording; hold it: clear the track
 *   SEQ         mute / unmute the track
 *   SAVE        hold it: keep the song (it comes back at power-on)
 *   OCT- OCT+   the track's octave
 *   EDIT        the scale (PENTA, PENTA MIN, MAJOR, MINOR, BLUES, DORIAN, ANY NOTE); ARP: the key
 *   SELECT      tempo; PRESETS: loop length (1, 2, 4, 8 bars); ALGORITHM: the track's sound
 *   KNOB1..4    TONE (drums: PITCH), SHAPE (synths: squelch, drums: decay), ECHO, LEVEL
 *
 * The song (the scale, key and every note) changes with EDIT and ARP: what was played follows. */
#include "groove.h"

song_t song;
typedef char song_fits[(sizeof(song_t) <= SETTINGS_MAX) ? 1 : -1];

static const char *const TRACK_NAME[TRACKS] = {"DRUMS", "BASS", "CHORDS", "LEAD"};
static const uint16_t TRACK_COLOR[TRACKS] = {RGB(255, 150, 40), RGB(70, 165, 255), RGB(90, 220, 110), RGB(255, 95, 185)};
static const uint8_t TRACK_BTN[TRACKS] = {BTN_FX, BTN_SEL, BTN_ENV, BTN_LFO};
static const char *const ROOT_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
#define GREY RGB(120, 120, 120)
#define DARK RGB(45, 45, 52)
#define RED RGB(255, 50, 50)
#define GREEN RGB(60, 230, 90)

static uint8_t cur, rec, playing;

/* ---- the defaults: a gentle tempo, two bars, C pentatonic: any key a toddler hits sounds good */
static void song_new(void) {
  for (uint32_t i = 0; i < sizeof song; i++) {
    ((uint8_t *)&song)[i] = 0;
  }
  song.magic = SONG_MAGIC;
  song.bpm = 100, song.bars = 2, song.root = 0, song.scale = 0, song.quant = 1;
  static const uint8_t KNOBS[TRACKS][4] = {{64, 64, 10, 100}, {64, 50, 8, 100}, {64, 64, 40, 90}, {64, 64, 45, 95}};
  for (int t = 0; t < TRACKS; t++) {
    for (int k = 0; k < 4; k++) {
      song.trk[t].knob[k] = KNOBS[t][k];
    }
  }
}

static int song_ok(void) {
  if (song.magic != SONG_MAGIC || song.bpm < BPM_MIN || song.bpm > BPM_MAX || song.scale >= SCALES || song.root > 11) {
    return 0;
  }
  if (song.bars != 1 && song.bars != 2 && song.bars != 4 && song.bars != 8) {
    return 0;
  }
  for (int t = 0; t < TRACKS; t++) {
    track_t *k = &song.trk[t];
    if (k->count > MAX_EV || k->oct < -2 || k->oct > 2) {
      return 0;
    }
    k->sound %= (uint8_t)sound_count(t);
    for (int j = 0; j < 4; j++) {
      k->knob[j] = k->knob[j] > 127 ? 127 : k->knob[j];
    }
    for (int i = 0; i < k->count; i++) {
      song.ev[t][i].vel &= 0x7F;
      song.ev[t][i].len = song.ev[t][i].len ? song.ev[t][i].len : 1;
    }
  }
  return 1;
}

/* ---- recording. A pass of the loop while REC is on is one take; HOME takes the last one back */
#define TAKES 32
static struct {
  uint8_t track;
  uint16_t count;
} take[TAKES];
static int ntakes;
static int16_t held_ev[KEY_COUNT];
static uint8_t held_trk[KEY_COUNT], held_note[KEY_COUNT];
static uint32_t held_at[KEY_COUNT];
static uint8_t lane_dirty = 0xF;

static void take_begin(void) {
  if (ntakes && take[ntakes - 1].track == cur && take[ntakes - 1].count == song.trk[cur].count) {
    return; /* the last take added nothing: keep it */
  }
  if (ntakes == TAKES) {
    for (int i = 1; i < TAKES; i++) {
      take[i - 1] = take[i];
    }
    ntakes--;
  }
  take[ntakes].track = cur, take[ntakes].count = song.trk[cur].count;
  ntakes++;
}

static uint32_t now_q8(void) { /* the playhead in 1/256 ticks */
  return (uint32_t)eng_pos * 256u + eng_frac;
}

static void record_on(int k, uint8_t note) {
  track_t *t = &song.trk[cur];
  held_ev[k] = -1;
  if (t->count >= MAX_EV) {
    return;
  }
  uint32_t L = loop_len(), now = now_q8(), pos = eng_pos, q = pos;
  int ahead = 0;
  if (song.quant) {
    q = (now + STEP * 128u) / (STEP * 256u) * STEP;
    if (q >= L) {
      q -= L;
    }
    ahead = q != pos && (q > pos || q == 0);
  }
  event_t *e = &song.ev[cur][t->count];
  e->tick = (uint16_t)q, e->note = note, e->vel = (uint8_t)(100 | (ahead ? 0x80 : 0));
  e->len = cur == T_DRUMS ? 1 : 0;
  BARRIER();
  if (cur != T_DRUMS) {
    held_ev[k] = (int16_t)t->count, held_at[k] = now;
  }
  t->count++;
  lane_dirty |= 1u << cur;
}

static void record_off(int k) {
  if (held_ev[k] < 0) {
    return;
  }
  int tr = held_trk[k];
  uint32_t L = loop_len() * 256u, d = (now_q8() + L - held_at[k]) % L;
  uint32_t len = d >> 8;
  len = len < 2 ? 2 : len >= L / 256u ? L / 256u - 1 : len;
  if (held_ev[k] < song.trk[tr].count) {
    song.ev[tr][held_ev[k]].len = (uint16_t)len;
  }
  held_ev[k] = -1;
  lane_dirty |= 1u << tr;
}

static void record_stop(void) {
  for (int k = 0; k < KEY_COUNT; k++) {
    record_off(k);
  }
}

/* ---- the screen */
static char msg[24];
static uint32_t msg_until;
static uint16_t msg_color;
static uint8_t head_dirty = 1, knobs_dirty = 1;

static void say(const char *s, uint16_t color) {
  int i = 0;
  for (; s[i] && i < 23; i++) {
    msg[i] = s[i];
  }
  msg[i] = 0;
  msg_until = millis() + 1300, msg_color = color, head_dirty = 1;
}

static char *cat(char *d, const char *s) {
  while (*s) {
    *d++ = *s++;
  }
  *d = 0;
  return d;
}

static void draw_head(void) {
  char s[24];
  rect(0, 0, SCREEN_W, 26, BLACK);
  if (msg[0] && (int32_t)(msg_until - millis()) > 0) {
    rect(0, 0, SCREEN_W, 24, msg_color);
    text((SCREEN_W - text_width(msg, FONT_SMALL)) / 2, 4, msg, BLACK, FONT_SMALL);
    return;
  }
  msg[0] = 0;
  format_int(s, song.bpm);
  cat(s + (song.bpm >= 100 ? 3 : 2), " BPM"); /* format_int: the digits, then 0 */
  text(4, 4, s, WHITE, FONT_SMALL);
  format_int(s, song.bars);
  cat(s + 1, song.bars == 1 ? " BAR" : " BARS");
  text(74, 4, s, GREY, FONT_SMALL);
  char *e = cat(s, ROOT_NAME[song.root]);
  e = cat(e, " ");
  cat(e, SCALE_NAME[song.scale]);
  text(136 - (song.scale == 1 || song.scale == 6 ? 8 : 0), 4, s, WHITE, FONT_SMALL);
  if (playing) { /* a play triangle */
    for (int i = 0; i < 7; i++) {
      line(214 + i, 6 + i, 214 + i, 18 - i, GREEN);
    }
  } else {
    rect(214, 7, 10, 10, GREY);
  }
  if (rec) {
    rect(228, 7, 10, 10, RED);
  }
  if (!song.quant) {
    text(196, 4, "~", GREY, FONT_SMALL);
  }
}

#define LANE_Y(t) (28 + (t) * 40)
#define STRIP_X 6
#define STRIP_W 230

static void draw_lane(int t) {
  int y0 = LANE_Y(t), sel = t == cur;
  track_t *k = &song.trk[t];
  uint16_t col = k->mute ? GREY : TRACK_COLOR[t];
  rect(0, y0, SCREEN_W, 38, sel ? DARK : BLACK);
  if (sel) {
    rect(0, y0, 3, 38, TRACK_COLOR[t]);
  }
  int x = text(STRIP_X, y0 + 2, TRACK_NAME[t], col, FONT_SMALL);
  text(x + 8, y0 + 2, sound_name(t, k->sound), sel ? WHITE : GREY, FONT_SMALL);
  if (k->mute) {
    text(SCREEN_W - 4 - text_width("MUTE", FONT_SMALL), y0 + 2, "MUTE", RED, FONT_SMALL);
  } else if (k->count >= MAX_EV) {
    text(SCREEN_W - 4 - text_width("FULL", FONT_SMALL), y0 + 2, "FULL", RED, FONT_SMALL);
  } else if (t != T_DRUMS && k->oct) {
    char s[12] = "+";
    format_int(s + (k->oct > 0), k->oct);
    text(SCREEN_W - 4 - text_width(s, FONT_SMALL), y0 + 2, s, GREY, FONT_SMALL);
  }
  uint32_t L = loop_len();
  for (uint32_t b = 0; b <= song.bars; b++) { /* the bars */
    rect(STRIP_X + (int)(b * BAR * STRIP_W / L) - (b == song.bars), y0 + 20, 1, 17, RGB(70, 70, 80));
  }
  for (int i = 0; i < k->count; i++) {
    event_t *e = &song.ev[t][i];
    if (e->tick >= L) {
      continue;
    }
    int ex = STRIP_X + (int)(e->tick * STRIP_W / L);
    int w = t == T_DRUMS ? 2 : e->len ? (int)(e->len * STRIP_W / L) : 3;
    w = w < 2 ? 2 : w;
    if (ex + w > STRIP_X + STRIP_W) {
      w = STRIP_X + STRIP_W - ex;
    }
    int ey;
    if (t == T_DRUMS) {
      ey = y0 + 21 + drum_of_key(note_key(e->note)) * 2;
    } else {
      int v = (note_oct(e->note) + 2) * 12 + note_key(e->note); /* 0..74 */
      ey = y0 + 35 - v * 14 / 74;
    }
    rect(ex, ey, w, 2, col);
  }
}

static int playhead_x = -1;
static void draw_playhead(void) {
  int x = playing ? STRIP_X + (int)((uint32_t)eng_pos * STRIP_W / loop_len()) : -1;
  if (x == playhead_x) {
    return;
  }
  playhead_x = x;
  rect(0, 189, SCREEN_W, 5, BLACK);
  if (x >= 0) {
    rect(x - 1, 189, 3, 5, rec ? RED : WHITE);
  }
}

static void draw_knobs(void) {
  static const char *const SYN[4] = {"TONE", "SHAPE", "ECHO", "LEVEL"}, *const DRM[4] = {"PITCH", "DECAY", "ECHO", "LEVEL"};
  rect(0, 196, SCREEN_W, 44, BLACK);
  for (int i = 0; i < 4; i++) {
    const char *n = cur == T_DRUMS ? DRM[i] : SYN[i];
    int x = i * 60;
    text(x + (60 - text_width(n, FONT_SMALL)) / 2, 198, n, GREY, FONT_SMALL);
    frame(x + 5, 218, 50, 12, RGB(80, 80, 90));
    rect(x + 6, 219, song.trk[cur].knob[i] * 48 / 127, 10, TRACK_COLOR[cur]);
  }
}

/* ---- the panel */
static int accel(int e, int32_t detents) { /* a slow turn: 1 a detent, a fast spin: up to 4 */
  static uint32_t last[ENC_COUNT];
  uint32_t now = millis(), dt = now - last[e];
  if (!detents) {
    return 0;
  }
  last[e] = now;
  return detents * (dt < 25 ? 4 : dt < 60 ? 2
                                          : 1);
}

static void set_play(int on) {
  if (on == playing) {
    return;
  }
  playing = (uint8_t)on;
  if (!on && rec) {
    rec = 0, eng_click = 0;
    record_stop();
  }
  engine_cmd(on ? CMD_PLAY : CMD_STOP, 0, 0, 0);
  head_dirty = 1, lane_dirty = 0xF;
}

static void select_track(int t) {
  if (t == cur) {
    return;
  }
  lane_dirty |= (uint8_t)(1u << cur | 1u << t);
  cur = (uint8_t)t, knobs_dirty = 1;
  if (rec) {
    take_begin();
  }
  say(TRACK_NAME[t], TRACK_COLOR[t]);
}

static void undo(void) {
  while (ntakes) {
    ntakes--;
    int t = take[ntakes].track;
    if (take[ntakes].count < song.trk[t].count) {
      song.trk[t].count = take[ntakes].count;
      engine_cmd(CMD_TRACK_OFF, (uint8_t)t, 0, 0);
      for (int k = 0; k < KEY_COUNT; k++) {
        if (held_trk[k] == t && held_ev[k] >= song.trk[t].count) {
          held_ev[k] = -1;
        }
      }
      lane_dirty |= 1u << t;
      say("UNDO", TRACK_COLOR[t]);
      if (rec) {
        take_begin();
      }
      return;
    }
  }
  say("NOTHING TO UNDO", GREY);
}

static void clear_track(void) {
  int n = 0;
  for (int i = 0; i < ntakes; i++) {
    if (take[i].track != cur) {
      take[n++] = take[i];
    }
  }
  ntakes = n;
  song.trk[cur].count = 0;
  for (int k = 0; k < KEY_COUNT; k++) {
    if (held_trk[k] == cur) {
      held_ev[k] = -1;
    }
  }
  engine_cmd(CMD_TRACK_OFF, cur, 0, 0);
  lane_dirty |= 1u << cur;
  say("CLEARED", RED);
  if (rec) {
    take_begin();
  }
}

static void save_song(void) {
  set_play(0);
  if (settings_save(&song, sizeof song) == 0) {
    say("SAVED", GREEN);
  } else {
    say("COULD NOT SAVE", RED);
  }
}

void setup(void) {
  engine_init();
  if (settings_load(&song, sizeof song) != 0 || !song_ok()) {
    song_new();
  }
  for (int k = 0; k < KEY_COUNT; k++) {
    held_ev[k] = -1;
  }
  clear(BLACK);
  led_backlight(1);
  say("MEWZAC", TRACK_COLOR[0]);
}

void loop(void) {
  static uint32_t home_at, save_at, last_wrap;
  static uint8_t home_done, save_done;
  track_t *t = &song.trk[cur];

  /* the keys */
  for (int k = 0; k < KEY_COUNT; k++) {
    if (key_pressed(k)) {
      uint8_t note = pack_note(k, cur == T_DRUMS ? 0 : t->oct);
      held_trk[k] = cur, held_note[k] = note;
      engine_cmd(CMD_ON, cur, note, 100);
      if (rec && playing) {
        record_on(k, note);
      }
    }
    if (key_released(k)) {
      engine_cmd(CMD_OFF, held_trk[k], held_note[k], 0);
      record_off(k);
    }
  }

  /* MIDI in plays the selected track too */
  uint8_t st, d1, d2;
  while (midi_read(&st, &d1, &d2)) {
    int rel = d1 - 53, oct = rel >= 0 ? rel / 12 : -((11 - rel) / 12);
    oct = oct < -2 ? -2 : oct > 2 ? 2
                                  : oct;
    int k = rel - 12 * oct;
    if (k < 0 || k >= KEY_COUNT) {
      continue;
    }
    if ((st & 0xF0) == 0x90 && d2) {
      engine_cmd(CMD_ON, cur, pack_note(k, oct), d2);
    } else if ((st & 0xF0) == 0x80 || (st & 0xF0) == 0x90) {
      engine_cmd(CMD_OFF, cur, pack_note(k, oct), 0);
    }
  }

  /* transport */
  if (button_pressed(BTN_PLAY)) {
    set_play(!playing);
  }
  if (button_pressed(BTN_REC)) {
    if (!rec) {
      rec = 1, eng_click = 1;
      set_play(1);
      take_begin();
      last_wrap = eng_wraps;
    } else {
      rec = 0, eng_click = 0;
      record_stop();
    }
    head_dirty = 1;
  }
  if (rec && eng_wraps != last_wrap) { /* every pass is a take */
    last_wrap = eng_wraps;
    take_begin();
  }

  /* tracks, mute, octave */
  for (int i = 0; i < TRACKS; i++) {
    if (button_pressed(TRACK_BTN[i])) {
      select_track(i);
      t = &song.trk[cur];
    }
  }
  if (button_pressed(BTN_SEQ)) {
    t->mute = !t->mute;
    if (t->mute) {
      engine_cmd(CMD_TRACK_OFF, cur, 0, 0);
    }
    lane_dirty |= 1u << cur;
    say(t->mute ? "MUTED" : "UNMUTED", TRACK_COLOR[cur]);
  }
  int oct = t->oct + button_pressed(BTN_OCT_UP) - button_pressed(BTN_OCT_DOWN);
  if (cur != T_DRUMS && oct != t->oct && oct >= -2 && oct <= 2) {
    t->oct = (int8_t)oct;
    lane_dirty |= 1u << cur;
  }

  /* undo / clear: HOME; save: hold SAVE */
  if (button_pressed(BTN_HOME)) {
    home_at = millis(), home_done = 0;
  }
  if (button_held(BTN_HOME) && !home_done && millis() - home_at > 800) {
    home_done = 1;
    clear_track();
  }
  if (button_released(BTN_HOME) && !home_done) {
    undo();
  }
  if (button_pressed(BTN_SAVE)) {
    save_at = millis(), save_done = 0;
    say("HOLD TO SAVE", GREY);
  }
  if (button_held(BTN_SAVE) && !save_done && millis() - save_at > 1000) {
    save_done = 1;
    save_song();
  }

  /* the song's feel */
  if (button_pressed(BTN_EDIT)) {
    song.scale = (uint8_t)((song.scale + 1) % SCALES);
    say(SCALE_NAME[song.scale], WHITE);
  }
  if (button_pressed(BTN_ARP)) {
    song.root = (uint8_t)((song.root + 1) % 12);
    char s[16];
    cat(cat(s, "KEY OF "), ROOT_NAME[song.root]);
    say(s, WHITE);
  }
  if (button_pressed(BTN_GLO)) {
    song.quant = !song.quant;
    say(song.quant ? "SNAP TO BEAT" : "FREE TIMING", WHITE);
  }
  int bpm = song.bpm + accel(ENC_SELECT, encoder(ENC_SELECT));
  bpm = bpm < BPM_MIN ? BPM_MIN : bpm > BPM_MAX ? BPM_MAX
                                                : bpm;
  if (bpm != song.bpm) {
    song.bpm = (uint16_t)bpm, head_dirty = 1;
  }
  int32_t pr = encoder(ENC_PRESETS);
  if (pr) {
    static const uint8_t BARS[4] = {1, 2, 4, 8};
    int i = song.bars == 1 ? 0 : song.bars == 2 ? 1
                               : song.bars == 4 ? 2
                                                : 3;
    i += pr > 0 ? 1 : -1;
    i = i < 0 ? 0 : i > 3 ? 3
                          : i;
    if (BARS[i] != song.bars) {
      song.bars = BARS[i], head_dirty = 1, lane_dirty = 0xF;
    }
  }
  int32_t al = encoder(ENC_ALGORITHM);
  if (al) {
    int n = sound_count(cur);
    t->sound = (uint8_t)(((t->sound + (al > 0 ? 1 : -1)) % n + n) % n);
    engine_cmd(CMD_TRACK_OFF, cur, 0, 0);
    lane_dirty |= 1u << cur;
  }
  for (int i = 0; i < 4; i++) {
    int v = t->knob[i] + accel(ENC_KNOB1 + i, encoder(ENC_KNOB1 + i)) * 2;
    v = v < 0 ? 0 : v > 127 ? 127
                            : v;
    if (v != t->knob[i]) {
      t->knob[i] = (uint8_t)v, knobs_dirty = 1;
    }
  }

  /* the lights: the keys show what plays on the selected track */
  uint32_t lit = engine_lit(cur) | keys();
  for (int k = 0; k < KEY_COUNT; k++) {
    led_key(k, lit >> k & 1);
  }
  for (int i = 0; i < TRACKS; i++) {
    led_button(TRACK_BTN[i], i == cur);
  }
  int beat = playing && eng_pos % PPQN < 6;
  led_button(BTN_PLAY, playing && (beat || !rec));
  led_button(BTN_REC, rec && (millis() & 256 ? 1 : beat));
  led_button(BTN_SEQ, t->mute);
  led_button(BTN_GLO, song.quant);

  /* the screen: only what changed */
  static uint32_t lane_at;
  if (head_dirty || (msg[0] && (int32_t)(msg_until - millis()) <= 0)) {
    head_dirty = 0;
    draw_head();
  }
  if (lane_dirty && millis() - lane_at > 60) {
    lane_at = millis();
    for (int i = 0; i < TRACKS; i++) {
      if (lane_dirty >> i & 1) {
        draw_lane(i);
      }
    }
    lane_dirty = 0;
  }
  if (knobs_dirty) {
    knobs_dirty = 0;
    draw_knobs();
  }
  draw_playhead();
}
