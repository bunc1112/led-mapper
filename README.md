# CHRONOS — 20 time-bending effects in one FFGL plugin for Resolume

People on the Resolume forum have been asking for a native **slit-scan** and **Rutt-Etra** FFGL plugin since 2015.
Right now the only ways to get them are feedback-layer tricks or Wire patches. CHRONOS is a native C++ FFGL 2.x effect
that keeps a **GPU ring buffer of past frames** (one `GL_TEXTURE_2D_ARRAY`), so every mode can read any
moment from the last 16–128 frames directly, with no feedback smearing.

![contact sheet](preview/chronos_contact_sheet.png)

## The 20 modes (choose them in the *Mode* dropdown)

| # | Mode | What it does | Param A | Param B |
|---|------|--------------|---------|---------|
| 0 | **Slit-Scan** | Each pixel shows a different moment in time, set by the *Time Map*: Left>Right, Top>Bottom, Radial, Spiral, Noise or **Luma** (bright areas show older frames). Neighbouring frames are blended, so there's no banding | Map Repeat | Map Speed (scrolls the map) |
| 1 | **Chroma Time Split** | Red is now, green is Depth/2 ago, blue is Depth ago, plus an optional spatial offset | Spread | Angle |
| 2 | **Beat Stutter** | Locks to the BPM. On each *Beat Division* it may loop the last few frames, like a beat-repeat for video | Loop Length | Chance |
| 3 | **Chrono Trails** | Finds motion between past frames and paints it in a colour for each age (rainbow time trails) | Sensitivity | Video Level |
| 4 | **Rutt-Etra** | Classic scanline rasterizer that lifts each line by its brightness. With Depth > 0, **each scanline also comes from a different moment** | Height | Lines |
| 5 | **Time Mosaic** | Blocks of the picture fall behind in time and reshuffle on the beat (datamosh feel) | Block Size | Chance |
| 6 | **Long Exposure** | Camera shutter: from *average* (motion blur) to *max* (light painting) | Light Paint | Decay |
| 7 | **Time Tunnel** | Older frames sink into the distance, and bright parts of newer frames sit in front of them | Zoom | Key Level |
| 8 | **Datamosh** | Real optical flow per 16 px macroblock drags the previous image along the motion (the P-frame "bloom"). On the beat, with *I-Frame Chance*, it snaps back to clean video | Mosh Strength | I-Frame Chance |
| 9 | **Pixel Melt** | Pixel-sort style drips: bright pixels run down into darker areas (sideways with Reverse). The further a drip falls, the further back in time it was sampled | Threshold | Length |
| 10 | **Reaction-Diffusion** | Live Gray-Scott simulation (8 steps per frame) that grows over the video and is fed by its brightness. Patterns go from coral to mitosis to worms | Pattern | Video Influence |
| 11 | **Infinite Feedback** | Classic video feedback that zooms, spins and slowly shifts hue, with no extra layers needed | Zoom / Spin | Persistence |
| 12 | **VHS Rewind** | Plays the buffer backwards like a rewinding tape, with tracking band, jitter, colour bleed, scanlines and noise. Reverse switches to normal-speed VHS playback | Rewind Speed | Tape Damage |
| 13 | **Video Scratch** | The playhead is scratched back and forth on every beat division, like a DJ on a turntable | Scratch Shape | Double Time |
| 14 | **Beat Ripple** | On each beat a shockwave travels out: ahead of it you see the past (Depth ago), behind it time snaps back to live | Wave Strength | Random Center |
| 15 | **Slice Shift** | Horizontal bands jump sideways and back in time with RGB split, reshuffling on the beat | Bands | Chance |
| 16 | **Strobe Echo** | Stroboscopic multiple exposure: 2–8 crisp copies of the past | Copies | Falloff |
| 17 | **Neon Echo** | Outlines of the last moments, each age glowing in its own neon colour | Sensitivity | Video Level |
| 18 | **Time Kaleido** | Kaleidoscope where every mirrored segment shows a different moment. With Reverse, time follows the radius instead | Segments | Spin |
| 19 | **Motion Key / Cloak** | B = 0 keeps only what moves (transparent elsewhere, great for layering). B = 1 turns movers into a see-through refractive "invisibility cloak" | Sensitivity | Key / Cloak |

**Shared controls:** Depth (how far back in time to look; the readout shows frames), Reverse Time (flips the direction;
in Time Mosaic it inverts blocks), Beat Division (1/16 … 1 bar; used by Beat Stutter, Time Mosaic, Datamosh, Video Scratch, Beat Ripple and Slice Shift), Freeze Buffer (stops recording so you can scrub a
frozen moment), Mix (dry/wet).
**Buffer group:** Buffer Frames (16/32/64/128) and Buffer Res (Full/Half/Quarter).

Small touches: Param A/B **change their names** for each mode (*Map Repeat*, *Loop Length*, *Light Paint*…), controls that
don't apply are hidden, and each mode **remembers its own A/B values**, so switching modes feels like switching
effects. These need Resolume 7.4 or newer; older 7.x versions just show "Param A / Param B".

### Feedback modes
Datamosh, Reaction-Diffusion and Infinite Feedback keep a half-float feedback buffer at *Buffer Res*. It restarts fresh each time you switch to one of them.

### VRAM
The buffer uses `width × height × 4 bytes × frames`. At 1080p, Half res with 64 frames is about 130 MB (the default).
Full res with 128 frames is about 1 GB. If allocation fails, the plugin logs a message and you can lower the settings.

## Build

### Easiest: GitHub Actions
Push this folder to a GitHub repo. `.github/workflows/build.yml` builds **Windows x64 `.dll`** and **macOS universal
`.bundle`** and attaches them as artifacts.

### Locally (CMake, fetches the Resolume FFGL SDK automatically)
```
cmake -S . -B build -A x64            # Windows (VS 2019/2022)
cmake --build build --config Release
# macOS
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

### Inside the official resolume/ffgl repo (Visual Studio)
Copy `source/Chronos` to `<ffgl>/source/plugins/Chronos` and `build/windows/Chronos.vcxproj(.filters)` to
`<ffgl>/build/windows/`. Then in `FFGLPlugins.sln` choose *Add > Existing Project* and build.

## Install
Copy `Chronos.dll` (Windows) or `Chronos.bundle` (macOS) to `Documents/Resolume Arena/Extra Effects`, then restart
Resolume. You'll find it under Effects → **Chronos**.

## Preview without Resolume
`tools/preview_harness.py` runs the exact plugin shaders headless (moderngl + EGL), and `tools/render_showreel.py`
renders `preview/chronos_showreel.mp4`. This is how the modes were tested. (`tools/dump_shaders.cpp` extracts the GLSL from the plugin source, so the preview always matches the plugin.)
```
pip install moderngl numpy pillow imageio imageio-ffmpeg
python tools/render_showreel.py
```

## Verified so far
- Builds and links against the current resolume/ffgl master and exports `plugMain` (checked on Linux).
- All 20 modes ran on a real OpenGL 4.x driver (Mesa) with the exact plugin GLSL `#version 410 core`.
- Not yet tested inside Resolume itself. Please report anything odd (Resolume's log shows `Chronos:` messages).

---

# CUTUP — beat-synced recap montage with text (second plugin)

Recreates the classic "year recap" edit: big words pop in letter by letter with the footage **inside** the letters,
then hard cuts, cards cascading and zooming, cream flash frames. All of it is cut **on the beat**
(Resolume's BPM) or with the **Next** button, with up to **16 text boxes**.

`preview/cutup_showreel.mp4` shows it. Left: video in shapes on a red vignette, with captions on cuts. Right: Holes
mode, where a second layer shows through the text and shapes.

## How to layer it
Put CUTUP on a layer (or clip) with your footage:
- **Output → Video in Shapes**: your footage only appears inside the words, cards and shapes. With *Background =
  Transparent*, everything else is see-through, so layers below fill the rest.
- **Output → Holes (Layer Below)**: the words and shapes are cut out of this layer, so **the video on the layer below
  is visible inside the text/shapes**.
- **Output → Matte**: white shapes on transparent, for Resolume's mask/alpha blend modes.

## Scenes (toggle each in the *Scenes* group)
| Scene | What happens |
|---|---|
| Word Reveal | The next text pops in letter by letter, filled with the video, then fades before the next cut |
| Word Knockout | Frame dims and the word punches through at full brightness |
| Full Frame | Hard cut with a zoom punch and vignette |
| Card Cascade | 4 cards fly in as a diagonal staircase, each a different crop |
| Card Zoom | Small card pops in centre, then grows to full screen |
| Card Scatter | 3–5 tilted cards pop in at random spots |
| Iris | Circle opens from the centre with a cream rim |
| Blinds | 8 vertical strips slide in from top and bottom |
| Grid | 3×3 tiles pop in, in random order |

**Flash Chance** adds cut accents: a cream card, a white flash or an orange light leak.

## Controls
- **Next** (button): jump to the next transition now. Map it to a key, MIDI or OSC.
- **Auto on Beat** + **Cut Every**: 1/4 beat … 4 bars, synced to Resolume's BPM.
- **Scene Order**: Sequential / Random / Shuffle (every scene once before repeats).
- **Text 1–16** (*Texts* group): empty boxes are skipped. Use `|` for a line break (e.g. `DROP|THE BASS`).
  Croatian and other Latin letters are supported (ČĆŽŠĐ, ÄÖÜ, É…).
- **Text Order**: Sequential / Random / Shuffle / Ping-Pong.
- **Letter Reveal**: Random Pop (like the reference), Left to Right, Slide Up, Scramble, All at Once.
- **Font**: Archivo Black (wide, heavy) or Anton (condensed). **Text Size**, **Text Position** (Center / Random /
  Top-Mid-Bottom), **Uppercase**.
- **Color** (picker) + **Text Tint**: 0 = pure video in letters, 1 = coloured letters with the video showing as texture.
- **Caption**: "Label on Cuts" writes the current word as a small cream label over card and cut scenes.
- **Background**: Transparent / Black / Colour Vignette / Dimmed Video / Video.
- **Anim Speed**, **Card Shadow**.

Fonts: Archivo Black and Anton, SIL Open Font License (see `fonts/`). They are baked into the plugin as a
signed-distance-field atlas (`tools/make_font_atlas.py`), so text stays sharp at any size.
The scene engine is plain C++ (`source/Cutup/Engine.cpp`). `tools/cutup_sim.cpp` + `tools/cutup_preview.py` run it
offline with the real shader to render previews.
