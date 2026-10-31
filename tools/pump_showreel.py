"""Renders preview/pump_showreel.mp4: four PUMP setups over the same two layers."""
import os
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pump_preview.py')).read())
F, FPS = 192, 24
FG = [foreground(f / FPS) for f in range(F)]; BG = [background(f / FPS) for f in range(F)]
foreground = lambda t: FG[min(int(round(t * FPS)), F - 1)]
background = lambda t: BG[min(int(round(t * FPS)), F - 1)]
cases = [
    ("Punch · every beat (like your clip)", ['style=0', 'pattern=0'], {}),
    ("Sidechain duck · keyed", ['style=1', 'pattern=0', 'decay=0.6'], dict(Opacity=0.8, Brightness=0.2, Glow=0.2, Zoom=0.0)),
    ("Strobe · 8ths · RGB split + shake", ['style=3', 'pattern=5', 'decay=0.4'], dict(Split=0.8, ShakeAmt=0.6, Glow=0.3, Opacity=0.7)),
    ("Audio kick · shockwave + defocus", ['source=1', 'audio=1', 'decay=0.5'], dict(Shock=0.9, Defocus=0.7, HueShift=0.15, Zoom=0.35)),
]
lab = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 14)
clips = [render(F, FPS, 124, args, u) for _, args, u in cases]
w = imageio.get_writer(os.path.join(ROOT, 'preview', 'pump_showreel.mp4'), fps=FPS, codec='libx264', quality=8, macro_block_size=8, ffmpeg_params=['-preset', 'veryfast'])
PH = H + 34
for f in range(F):
    g = Image.new('RGB', (W * 2 + 8, PH * 2 + 8), (16, 16, 20)); d = ImageDraw.Draw(g)
    for i, (name, _, _) in enumerate(cases):
        x, y = (i % 2) * (W + 8), (i // 2) * (PH + 8)
        img, env = clips[i][f]
        g.paste(Image.fromarray((img * 255).astype(np.uint8)), (x, y + 34))
        d.text((x + 8, y + 3), name, fill=(255, 255, 255), font=lab)
        d.rectangle([x + 8, y + 22, x + 8 + 300, y + 29], outline=(80, 80, 90))
        d.rectangle([x + 8, y + 22, x + 8 + int(300 * float(env)), y + 29], fill=(230, 240, 80))
    w.append_data(np.array(g))
    if f in (31, 37): g.save(f'/tmp/pumpreel_{f}.png')
w.close()
# brightness trace of the "like your clip" setup, centre region (same measure used on the reference)
tr = [float((c[0][60:210, 200:440].mean()) * 255) for c in clips[0][:60]]
print('trace', [round(v) for v in tr])
