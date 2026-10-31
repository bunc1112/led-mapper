"""PUMP preview: real envelope engine (pump_sim.cpp) + real shader, composited over a background layer."""
import os, re, math, subprocess, tempfile, numpy as np, moderngl, imageio
from PIL import Image, ImageDraw, ImageFont, ImageFilter
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.join(HERE, '..')
tmp = tempfile.mkdtemp(); SIM = os.path.join(tmp, 'sim')
subprocess.check_call(['g++', '-std=c++14', '-O2', os.path.join(HERE, 'pump_sim.cpp'), '-o', SIM])
src = open(os.path.join(ROOT, 'source/Pump/Shaders.h')).read()
VS = re.search(r'vertexShader\[\] = R"\((.*?)\)";', src, re.S).group(1)
FS = re.search(r'fragmentShader\[\] = R"\((.*?)\)";', src, re.S).group(1)
W, H = 640, 270
ctx = moderngl.create_standalone_context(backend='egl', require=410)
quad = ctx.buffer(np.array([-1,-1,0,1,0,0, 1,-1,0,1,1,0, -1,1,0,1,0,1, 1,1,0,1,1,1], 'f4'))
prog = ctx.program(vertex_shader=VS, fragment_shader=FS)
vao = ctx.vertex_array(prog, [(quad, '4f 2f', 'vPosition', 'vUV')])
tin = ctx.texture((W, H), 4); tin.repeat_x = tin.repeat_y = False
out = ctx.texture((W, H), 4); fbo = ctx.framebuffer([out])
rng = np.random.default_rng(5)
Y, X = np.mgrid[0:H, 0:W] / np.array([H, W])[:, None, None]

def background(t):
    """Layer below: cyan sky, drifting clouds and light beams (like the LED wall in the reference)."""
    n = np.zeros((H, W))
    for o in range(4):
        f = 2 ** o * 3
        n += np.sin(X * f * 3.1 + t * 0.2 * (o + 1) + np.sin(Y * f * 2.3 + o)) * np.cos(Y * f * 2.7 - t * 0.15) / (o + 1)
    cloud = np.clip(n * 0.35 + 0.35, 0, 1) ** 2 * (0.5 + Y * 0.6)
    img = np.stack([0.0 + 0.25 * cloud, 0.35 + 0.55 * cloud, 0.55 + 0.45 * cloud], -1) * (0.35 + 0.65 * (1 - Y))[..., None]
    for k in range(4):
        cx = 0.15 + 0.23 * k + 0.05 * math.sin(t * 0.5 + k)
        beam = np.clip(1 - np.abs(X - cx - (Y - 0.0) * (0.4 if k % 2 else -0.4)) * 12, 0, 1) * (1 - Y) * 0.6
        img += beam[..., None] * np.array([0.3, 0.9, 1.0])
    return np.clip(img, 0, 1)

TENDRILS = [(rng.uniform(0, math.tau), rng.uniform(0.5, 1.0), rng.uniform(-2, 2), rng.uniform(0, 6)) for _ in range(60)]
def foreground(t):
    """Top layer: yellow-green 'roots' radiating from the centre on black (the pumping element)."""
    im = Image.new('RGB', (W, H), (0, 0, 0)); d = ImageDraw.Draw(im)
    cx, cy = W * 0.48, H * 0.5
    for a0, ln, curl, ph in TENDRILS:
        pts = []
        for s in np.linspace(0, ln, 24):
            a = a0 + curl * s * 0.5 + 0.15 * math.sin(t * 0.8 + ph + s * 3)
            r = s * W * 0.42
            pts.append((cx + math.cos(a) * r, cy + math.sin(a) * r * 0.55))
        d.line(pts, fill=(200, 230, 60), width=2)
        for (x, y) in pts[::3]:
            rr = 1.5 + rng.random() * 2.5
            d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=(230, 245, 90))
    im = Image.blend(im, im.filter(ImageFilter.GaussianBlur(3)), 0.35)
    a = np.asarray(im, np.float32) / 255
    return a

def render(frames, fps=24, bpm=124, args=(), u=None):
    binf = os.path.join(tmp, 'e.bin')
    subprocess.check_call([SIM, binf, str(frames), str(fps), str(bpm)] + list(args))
    E = np.fromfile(binf, np.float32).reshape(frames, 4)
    U = dict(Brightness=0.6, Glow=0.5, Zoom=0.25, Opacity=0.55, Saturation=0.2, Split=0.0, ShakeAmt=0.0, Defocus=0.0, HueShift=0.0, Shock=0.0,
             KeyMode=1, KeyLevel=0.12, KeySoft=0.25, KeyPump=0.3)
    if u: U.update(u)
    res = []
    for f in range(frames):
        t = f / fps
        fg = foreground(t); rgba = np.concatenate([fg, np.ones((H, W, 1), np.float32)], -1)
        tin.write((rgba[::-1] * 255).astype(np.uint8).tobytes())
        fbo.use(); tin.use(0)
        V = dict(InputTexture=0, MaxUV=(1.0, 1.0), Resolution=(float(W), float(H)), Env=float(E[f, 0]), SinceHit=float(min(E[f, 1], 10)),
                 Shake=(float(E[f, 2]), float(E[f, 3]))); V.update(U)
        for k, v in V.items():
            if k in prog: prog[k].value = v
        vao.render(moderngl.TRIANGLE_STRIP)
        o = np.frombuffer(fbo.read(components=4), np.uint8).reshape(H, W, 4)[::-1].astype(np.float32) / 255
        comp = o[..., :3] + background(t) * (1 - o[..., 3:4])
        res.append((np.clip(comp, 0, 1), E[f, 0]))
    return res
