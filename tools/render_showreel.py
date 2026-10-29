"""Renders every CHRONOS mode with the real shaders into preview/ (contact sheet + showreel)."""
import os, numpy as np, imageio
from PIL import Image, ImageDraw, ImageFont
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'preview_harness.py')).read())
OUT = os.path.join(HERE, '..', 'preview')
lab = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 15)
cases = [("0 Slit-Scan · Spiral", 0, dict(TimeMap=3, ParamA=.3, ParamB=.2)),
 ("1 Chroma Time Split", 1, dict(ParamA=.3, Depth=.25)),
 ("2 Beat Stutter", 2, dict(ParamA=.4, ParamB=.6)),
 ("3 Chrono Trails", 3, dict(ParamA=.7, ParamB=.5, Depth=.6)),
 ("4 Rutt-Etra", 4, dict(ParamA=.45, ParamB=.25, Depth=.4)),
 ("5 Time Mosaic", 5, dict(ParamA=.35, ParamB=.45)),
 ("6 Long Exposure", 6, dict(ParamA=.85, ParamB=.3, Depth=.5)),
 ("7 Time Tunnel", 7, dict(ParamA=.35, ParamB=.12, Depth=.9)),
 ("8 Datamosh", 8, dict(ParamA=.5, ParamB=.08)),
 ("9 Pixel Melt", 9, dict(ParamA=.45, ParamB=.5, Depth=.3)),
 ("10 Reaction-Diffusion", 10, dict(ParamA=.3, ParamB=.6)),
 ("11 Infinite Feedback", 11, dict(ParamA=.7, ParamB=.6)),
 ("12 VHS Rewind", 12, dict(ParamA=.35, ParamB=.5, Depth=.9)),
 ("13 Video Scratch", 13, dict(ParamA=.4, ParamB=0., Depth=.5)),
 ("14 Beat Ripple", 14, dict(ParamA=.6, ParamB=.3)),
 ("15 Slice Shift", 15, dict(ParamA=.4, ParamB=.35)),
 ("16 Strobe Echo", 16, dict(ParamA=.4, ParamB=.4, Depth=.5)),
 ("17 Neon Echo", 17, dict(ParamA=.6, ParamB=.35, Depth=.4)),
 ("18 Time Kaleido", 18, dict(ParamA=.25, ParamB=.6, Depth=.6)),
 ("19 Motion Key / Cloak", 19, dict(ParamA=.5, ParamB=0., Depth=.6))]
F = 100; clips = []
for name, m, p in cases:
    ims = run(m, p, frames=F)
    for im in ims:
        d = ImageDraw.Draw(im); d.rectangle([0, H-24, W, H], fill=(0, 0, 0)); d.text((8, H-21), name, fill=(255, 255, 255), font=lab)
    clips.append(ims); print(name, flush=True)
cols, rows = 5, 4
w = imageio.get_writer(os.path.join(OUT, 'chronos_showreel.mp4'), fps=25, codec='libx264', quality=8, macro_block_size=8, ffmpeg_params=['-preset', 'veryfast'])
for f in range(F):
    g = Image.new('RGB', (W*cols, H*rows))
    for i, c in enumerate(clips): g.paste(c[f], ((i % cols)*W, (i//cols)*H))
    w.append_data(np.array(g))
    if f == 70: g.save(os.path.join(OUT, 'chronos_contact_sheet.png'))
w.close()
