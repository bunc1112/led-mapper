"""Renders preview/cutup_showreel.mp4 with the real engine + shader."""
import os
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'cutup_preview.py')).read())
F = 300
left = render(F, background=2, output=0, args=['div=2', 'order=0', 'overlay=1', 'flash=0.4', 'reveal=0', 'tint=0.6'])
right = render(F, background=0, output=1, composite_below=True,
               args=['div=2', 'order=2', 'reveal=3', 'font=1', 'size=0.6', 'flash=0', 'scenes=0134568', 'text6=DROP|THE BASS'])
lab = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 15)
w = imageio.get_writer(os.path.join(ROOT, 'preview', 'cutup_showreel.mp4'), fps=25, codec='libx264', quality=8, macro_block_size=8, ffmpeg_params=['-preset', 'veryfast'])
for f in range(F):
    g = Image.new('RGB', (W * 2 + 8, H + 30), (20, 20, 24))
    g.paste(left[f], (0, 30)); g.paste(right[f], (W + 8, 30))
    d = ImageDraw.Draw(g)
    d.text((10, 7), "Video in Shapes · Colour Vignette · Caption on cuts", fill=(255, 255, 255), font=lab)
    d.text((W + 18, 7), "Holes → layer below shows through · Scramble · Anton", fill=(255, 255, 255), font=lab)
    w.append_data(np.array(g))
    if f in (12, 40, 135, 215): g.save(f'/tmp/reel_{f}.png')
w.close()
