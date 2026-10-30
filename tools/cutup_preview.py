"""Renders CUTUP previews: real engine (tools/cutup_sim.cpp) + real shader (source/Cutup/Shaders.h).
Two layers are simulated: a 'concert' clip under the effect, and a second clip on the layer below."""
import os, re, sys, math, subprocess, tempfile, numpy as np, moderngl, imageio
from PIL import Image, ImageDraw, ImageFont, ImageFilter
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.join(HERE, '..')
tmp = tempfile.mkdtemp()
SIM = os.path.join(tmp, 'sim')
subprocess.check_call(['g++', '-std=c++14', '-O2', os.path.join(HERE, 'cutup_sim.cpp'), os.path.join(ROOT, 'source/Cutup/Engine.cpp'), '-o', SIM])
src = open(os.path.join(ROOT, 'source/Cutup/Shaders.h')).read()
VS = re.search(r'vertexShader\[\] = R"\((.*?)\)";', src, re.S).group(1)
FS = re.search(r'fragmentShader\[\] = R"\((.*?)\)";', src, re.S).group(1)
hdr = open(os.path.join(ROOT, 'source/Cutup/FontAtlas.h')).read()
AW, AH = map(int, re.search(r'kAtlasW = (\d+), kAtlasH = (\d+)', hdr).groups())
atlas = np.array(list(map(int, re.search(r'kAtlas\[ \d+ \] = \{(.*?)\};', hdr, re.S).group(1).replace('\n', '').strip(',').split(','))), np.uint8)

W, H = 640, 360
ctx = moderngl.create_standalone_context(backend='egl', require=410)
quad = ctx.buffer(np.array([-1,-1,0,1,0,0, 1,-1,0,1,1,0, -1,1,0,1,0,1, 1,1,0,1,1,1], 'f4'))
prog = ctx.program(vertex_shader=VS, fragment_shader=FS)
vao = ctx.vertex_array(prog, [(quad, '4f 2f', 'vPosition', 'vUV')])
tex_in = ctx.texture((W, H), 4)
tex_atlas = ctx.texture((AW, AH), 1, atlas.tobytes()); tex_atlas.filter = (moderngl.LINEAR, moderngl.LINEAR)
tex_prims = ctx.texture((4, 160), 4, dtype='f4'); tex_prims.filter = (moderngl.NEAREST, moderngl.NEAREST)
out = ctx.texture((W, H), 4); fbo = ctx.framebuffer([out])

def concert(t):
    """Stand-in footage for the effect layer: stage lights, beams and a crowd."""
    y = np.linspace(0, 1, H)[:, None]; x = np.linspace(0, 1, W)[None, :]
    r = 0.55 + 0.45 * np.sin(t * 0.7 + x * 3); g = 0.25 + 0.25 * np.sin(t * 0.9 + y * 4 + 1); b = 0.6 + 0.4 * np.cos(t * 0.5 + x * 2 - y)
    one = np.ones((H, W)); img = np.stack([r * (0.4 + 0.6 * y) * one, g * (0.3 + 0.7 * y) * one, b * (0.5 + 0.5 * y) * one], -1)
    for k in range(5):  # light beams
        cx = 0.5 + 0.45 * math.sin(t * (0.8 + k * 0.17) + k * 1.3)
        beam = np.clip(1 - np.abs((x - cx) - (y - 1) * 0.35 * math.sin(t + k)) * 9, 0, 1) * y
        col = np.array([[1, .85, .4], [1, .3, .6], [.4, .8, 1], [1, 1, 1], [.9, .4, 1]][k])
        img += beam[..., None] * col * 0.6
    im = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8)); d = ImageDraw.Draw(im)
    for k in range(26):  # crowd
        cx = (k / 25) * W + math.sin(t * 3 + k) * 4; hop = abs(math.sin(t * 4.2 + k * 0.7)) * 8
        d.ellipse([cx - 22, H - 70 - hop, cx + 22, H + 40], fill=(12, 8, 20)); d.ellipse([cx - 11, H - 98 - hop, cx + 11, H - 72 - hop], fill=(12, 8, 20))
        if k % 6 == 0: d.line([cx + 8, H - 80 - hop, cx + 24, H - 130 - hop], fill=(12, 8, 20), width=7)
    return im

def below(t):
    """Stand-in footage for the layer underneath: cyan/blue moving stripes."""
    y = np.linspace(0, 1, H)[:, None]; x = np.linspace(0, 1, W)[None, :]
    s = 0.5 + 0.5 * np.sin((x * 14 + y * 6) - t * 5)
    img = np.stack([0.05 + 0.1 * s, 0.5 + 0.45 * s * y, 0.75 + 0.25 * s], -1)
    return Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))

def render(frames, fps=25, bpm=120, output=0, background=2, color=(0.6, 0.03, 0.05), args=(), composite_below=False):
    binf = os.path.join(tmp, 'p.bin')
    subprocess.check_call([SIM, binf, str(frames), str(fps), str(bpm), str(W / H)] + list(args))
    data = open(binf, 'rb').read(); off = 0; res = []
    for f in range(frames):
        n = int(np.frombuffer(data, np.int32, 1, off)[0]); off += 4
        prims = np.frombuffer(data, np.float32, n * 16, off); off += n * 64
        t = f / fps
        tex_in.write(np.array(concert(t).convert('RGBA'))[::-1].tobytes())
        if n: tex_prims.write(prims.tobytes(), viewport=(0, 0, 4, n))
        fbo.use(); tex_in.use(0); tex_atlas.use(1); tex_prims.use(2)
        for k, v in dict(InputTexture=0, Atlas=1, Prims=2, MaxUV=(1.0, 1.0), Resolution=(float(W), float(H)), AtlasSize=(float(AW), float(AH)),
                         Count=n, TextColor=color, Output=output, Background=background).items():
            if k in prog: prog[k].value = v
        vao.render(moderngl.TRIANGLE_STRIP)
        a = np.frombuffer(fbo.read(components=4), np.uint8).reshape(H, W, 4)[::-1].astype(np.float32) / 255
        # composite over the layer below (Resolume does this: premultiplied "over")
        under = np.asarray(below(t), np.float32) / 255 if composite_below else np.zeros((H, W, 3), np.float32)
        rgb = a[..., :3] + under * (1 - a[..., 3:4])
        res.append(Image.fromarray((rgb * 255).astype(np.uint8)))
    return res
