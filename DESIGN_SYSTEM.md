# Design system: "Space Age Flat"

This is the design system for the MEWZAC companion web app, which runs on the Pixel Fold and pairs with the
M-VAVE FM-1. Sections 1–6 are the original specification, generated with Google Gemini and lightly corrected.
Section 7 lists what was added for this app: a dark theme, layouts for the Fold, touch sizes and contrast fixes.
Where the two disagree, section 7 wins.

## 1. Aesthetic direction and core principles

The interface is flat and 2D. It draws on 1970s Japanese consumer electronics (the National Panasonic
"Do-Re-Mi" and "Panapic" lines) and on European Space Age industrial design. It should feel playful, modular,
tactile and instrument-grade without imitating real hardware.

### Hard constraints

- **No 3D, no skeuomorphism.** No `box-shadow`, inner shadows, highlights, glossy gradients, blur or
  glassmorphism, or bevels.
- **Depth comes from contrast and strokes.** Build hierarchy only with flat colour blocks, nested rounded
  containers and crisp 1.5–2px solid outlines.
- **No pure white or pure black.** Never use `#FFFFFF` or `#000000`. Use warm cream for light surfaces and
  charcoal for dark surfaces and ink.
- **Organic geometry.** Use pill shapes (`9999px`) and generous corner radii (10–24px). No sharp 90° corners on
  any interactive or structural element.
- **Landmark colour coding.** The two accents, vermilion and marigold, mark functions only: record, the playhead,
  active controls, drum groups and orientation keys. Never flood the screen with them.

## 2. Design tokens (light theme)

```css
:root {
  /* surfaces */
  --bg-canvas: #F6F3EB;          /* warm cream: the app background */
  --bg-surface: #EFECE2;         /* warm putty: module background */
  --bg-surface-muted: #E6E1D3;   /* inactive pill buttons, subtle tracks */
  --bg-dark: #23252A;            /* charcoal "hardware": track lanes, keyboard housing, active controls */
  --bg-dark-elevated: #2F3238;   /* step and grid contrast inside dark modules */

  /* accents */
  --accent-vermilion: #E85234;   /* record, playhead, primary action, landmark 1 */
  --accent-marigold: #F2B438;    /* second highlight, drum steps, dial arcs, landmark 2 */
  --neutral-inactive: #B8B4A7;   /* dial tracks, unlit pads, disabled (not for text) */

  /* ink and borders */
  --text-primary: #1E2024;
  --text-inverse: #F6F3EB;
  --text-muted: #5F5D56;         /* darkened from #6C6A63 for contrast (see 7.4) */
  --border-strong: #1E2024;      /* 2px outline: hardware modules, pads, keys */
  --border-subtle: #D6D1C4;      /* 1.5px outline: outer trays and decks */

  /* type */
  --font-display: 'Fredoka', 'Outfit', sans-serif;               /* wordmark, playful headings */
  --font-ui: 'Space Grotesk', 'Plus Jakarta Sans', sans-serif;   /* section titles, hardware labels */
  --font-mono: 'JetBrains Mono', 'IBM Plex Mono', monospace;     /* timecodes, BPM, numbers */

  /* radii */
  --radius-pill: 9999px;
  --radius-module: 20px;
  --radius-lane: 14px;
  --radius-pad: 10px;
  --radius-key: 0 0 8px 8px;     /* flat top, rounded bottom */

  /* strokes and spacing */
  --stroke-strong: 2px solid var(--border-strong);
  --stroke-subtle: 1.5px solid var(--border-subtle);
  --gap-grid: 14px;
  --pad-module: 16px;
}
```

## 3. Typography

- **Section headers:** `--font-ui`, 18–20px, weight 700, sentence case ("Timeline", "Control deck").
- **Hardware labels:** `--font-ui`, 11–13px, weight 700, UPPERCASE, letter-spacing 0.06em (REC, PLAY, TONE,
  DRUM PADS).
- **Telemetry:** `--font-mono`, 12–14px, weight 600, `tabular-nums` (0:05, 120 BPM).
- **Contrast rule:** always use dark ink on marigold. Never put cream text on yellow.

## 4. Layout

- **Phone portrait first:** a single-column stack with 14px gaps and 16px outer padding on `--bg-canvas`.
- **Modules ("bento"):** related tools are grouped in rounded containers (`--radius-module`). From top to bottom:
  - header: logo and the FM-1 status
  - transport tray: a pill with equal-width pill buttons
  - timeline: `--stroke-strong`
  - control deck and drum pads, side by side 50/50
  - keyboard: a dark module anchored at the bottom

## 5. Components

### A. Transport

- Pill buttons 44px or taller in a pill tray (`--stroke-subtle`, 6px padding).
- Default state: `--bg-surface-muted` background with `--text-primary` text.
- Active state: `--bg-dark` background with `--text-inverse` text.
- REC has an 8px vermilion dot. While recording, the whole button turns vermilion.

### B. Timeline (the tape)

- The container has a `--stroke-strong` outline.
- A ruler shows monospace bar numbers and times, with 4px tick marks.
- Track lanes:
  - Each lane is `--bg-dark`, `--radius-lane`, with an 8px gap between lanes.
  - The left column holds the track number, name, and the M (mute) and S (solo) buttons.
  - The right column draws the content:
    - audio: a vermilion waveform
    - steps: vertical pills in marigold and vermilion
    - clips: cream cards with charcoal waveforms
    - notes: rounded blocks
- The playhead is a 2px vermilion line through every lane, with a pill handle on the ruler.

### C. Rotary dials

- Dials are flat SVG:
  - a 270° track in `--neutral-inactive`, 6px wide
  - a value arc in vermilion that turns marigold past 60%
  - a charcoal core, set inside a 4px ring of the surface colour
  - a 2px pointer, or a monospace readout (such as tempo)
  - an uppercase label underneath
- Drag up or down, scroll the wheel, or use the arrow keys to turn a dial.

### D. Drum pads

- 4 × 4 square pads with `--radius-pad`, `--stroke-strong` and an 8px gap.
- Each row is one sound family, coloured flat:
  - kicks: vermilion
  - snares and claps: marigold
  - hats: cream
  - toms and effects: inactive grey

### E. Keyboard

- The housing is `--bg-dark`, `--radius-module`, with 12px padding.
- A utility strip shows the title, the target track, the bar range and an octave −/+ control.
- White keys are cream with a 1.5px outline. Black keys are charcoal at 60% height. All keys have rounded bottom
  corners.
- **Landmark keys:** every C is vermilion and every F is marigold, so you can find your place without labels.
- A key that is sounding fills with its track's accent colour, flat, with no glow.

## 6. Motion

- Transitions are fast and mechanical: 100–150ms, `cubic-bezier(0.2, 0, 0, 1)`.
- Pressing a control scales it to `0.96` and shifts its fill. No glow or shadow, ever.
- With `prefers-reduced-motion`, keep the playhead and turning tape but drop the press scaling and spinning
  decorations.

## 7. Additions for this app

### 7.1 Dark theme

The dark theme is built on a charcoal canvas with lighter charcoal modules. Hardware parts (lanes, keyboard
housing) go one step darker than the canvas, so they still read as "hardware", and outlines become light strokes.

```css
:root[data-theme="dark"],               /* also inside @media (prefers-color-scheme: dark) */
                                        /* on :root:not([data-theme="light"]) */
{
  --bg-canvas: #1D1F23;
  --bg-surface: #272A2F;
  --bg-surface-muted: #32353B;
  --bg-dark: #15171A;            /* hardware sits darker than the canvas */
  --bg-dark-elevated: #24272C;
  --accent-vermilion: #F0603F;   /* a touch brighter to hold contrast on charcoal */
  --accent-marigold: #F4BE4A;
  --neutral-inactive: #5A5D63;
  --text-primary: #EFEADD;
  --text-inverse: #EFEADD;
  --text-muted: #A6A296;
  --border-strong: #0F1012;      /* outlines on light parts (pads, keys) stay dark */
  --border-subtle: #3A3D44;
  color-scheme: dark;
}
```

- White keys and cream pads keep their cream (`#E9E4D6` in dark) so the instrument still reads as an object.
  Text on them stays dark.
- The theme follows the system by default. A switch in settings can force light or dark.

### 7.2 Layouts for the Pixel Fold

All layouts use the same modules. The arrangement changes with the screen, detected with container queries and
the viewport segment media queries (`vertical-viewport-segments: 2` for the half-folded tabletop posture).

| Posture | Screen | Arrangement |
| --- | --- | --- |
| **Closed** | Outer screen, portrait (about 412px wide) | The spec's single column: header, transport, timeline, control deck and pads, keyboard. |
| **Open, flat** | Inner screen (about 840 × 700) | Header and transport across the top. Timeline on the left, pads on the right. Keyboard and control deck along the bottom. |
| **Tabletop** | Inner screen, half-folded with the hinge across | **Top half (the display):** header, transport, timeline. **Bottom half (the controls):** pads, keyboard, control deck, side by side. Nothing interactive crosses the hinge. |
| **Beside the FM-1** | Any | Same as tabletop or open. The FM-1's keys play the selected track. |

### 7.3 Touch sizes

The app is played by adults and a three-year-old, so these sizes override the spec:

- Every tap target is at least **44 × 44px**. M and S buttons are 32px circles with a 44px hit area.
- Drum pads are at least 56px in the open and tabletop layouts.
- Keyboard keys are at least 28px wide on the outer screen.
- A pad or key responds on pointer-down, not on release.

### 7.4 Contrast

- `--text-muted` is `#5F5D56` in light and `#A6A296` in dark, readable on every surface.
- `--neutral-inactive` is used only for shapes (dial tracks, unlit pads), never for text that carries
  information.
- An active M or S button also changes shape (it fills in) as well as colour.

### 7.5 New components

- **FM-1 status chip:** a pill in the header with a dot. Vermilion means connected, grey means not connected.
  Tapping it connects over USB MIDI.
- **Tape counter:** a monospace readout (bar.beat) next to two small reels that turn while the tape runs.
- **Track select:** tap a lane's name to choose which track the keyboard plays. The selected lane gets a vermilion
  outline.
