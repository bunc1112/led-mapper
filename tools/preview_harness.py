"""Runs the exact CHRONOS shaders headless (moderngl + EGL) and mirrors the plugin's C++ frame logic."""
import os, math, subprocess, tempfile, numpy as np, moderngl
from PIL import Image, ImageDraw, ImageFont
HERE = os.path.dirname(os.path.abspath(__file__))
tmp = tempfile.mkdtemp()
subprocess.check_call(['g++', '-std=c++14', os.path.join(HERE, 'dump_shaders.cpp'), '-o', os.path.join(tmp, 'dump')])
subprocess.check_call([os.path.join(tmp, 'dump')], cwd=tmp)
sh = {k: open(os.path.join(tmp, k + '.glsl')).read() for k in ['vertex', 'copy', 'state', 'main']}

W, H, N = 384, 216, 64
ctx = moderngl.create_standalone_context(backend='egl', require=410)
quad = ctx.buffer(np.array([-1,-1,0,1,0,0, 1,-1,0,1,1,0, -1,1,0,1,0,1, 1,1,0,1,1,1], 'f4'))
def prog(fs):
    p = ctx.program(vertex_shader=sh['vertex'], fragment_shader=fs)
    return p, ctx.vertex_array(p, [(quad, '4f 2f', 'vPosition', 'vUV')])
pc, vc = prog(sh['copy']); ps, vs = prog(sh['state']); pm, vm = prog(sh['main'])
hist = ctx.texture_array((W, H, N), 4); hist.filter = (moderngl.LINEAR, moderngl.LINEAR)
hist.repeat_x = hist.repeat_y = False
inp = ctx.texture((W, H), 4); out = ctx.texture((W, H), 4); fbo_out = ctx.framebuffer([out])
layer = ctx.texture((W, H), 4); fbo_layer = ctx.framebuffer([layer])
state = [ctx.texture((W, H), 4, dtype='f2') for _ in range(2)]
for s in state: s.repeat_x = s.repeat_y = False
fbo_state = [ctx.framebuffer([s]) for s in state]
font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 40)

def frame(t):
    im = Image.new('RGB', (W, H), (8, 8, 20)); d = ImageDraw.Draw(im)
    for i in range(7):
        x = W/2 + math.sin(t*1.3+i)*W*0.38; y = H/2 + math.cos(t*0.9+i*1.7)*H*0.32; r = 14 + 8*math.sin(t*2+i)
        c = tuple(int(127+127*math.sin(i*0.9+k*2.1)) for k in range(3)); d.ellipse([x-r, y-r, x+r, y+r], fill=c)
    a = math.sin(t*2.2)*1.1; cx, cy = W*0.5, H*0.55
    d.line([cx, cy, cx+math.sin(a)*90, cy-math.cos(a)*90], fill=(255, 255, 255), width=10)
    d.text((W*0.5-100+math.sin(t)*60, 10), "CHRONOS", fill=(255, 200, 40), font=font)
    return np.array(im.convert('RGBA'))[::-1].copy()

def setu(p, **k):
    for n, v in k.items():
        if n in p: p[n].value = v

STATEFUL = {8, 10, 11}
def run(mode, params, frames=100, fps=25, warm=64, bpm=128.0, division=0.5):
    """Mirrors Chronos::ProcessOpenGL + UpdateBeat. Returns list of PIL frames."""
    imgs = []; head = -1; valid = 0; cur = 0
    rng = np.random.default_rng(3); slice_idx = None; slice_counter = 0; stut = False; sstart = 0; loop = 8; refresh = True
    for fs_ in fbo_state: fs_.use(); ctx.clear(0, 0, 0, 0)
    P = dict(ParamA=0.0, ParamB=0.5, Depth=0.8, Reverse=0.0, TimeMap=0, Mix=1.0); P.update(params)
    for f in range(warm + frames):
        t = f / fps; inp.write(frame(t).tobytes())
        head = (head + 1) % N; valid = min(valid + 1, N)
        fbo_layer.use(); inp.use(0); setu(pc, InputTexture=0, MaxUV=(1.0, 1.0)); vc.render(moderngl.TRIANGLE_STRIP)
        hist.write(fbo_layer.read(components=4), viewport=(0, 0, head, W, H, 1))
        beats = math.fmod(t * bpm / 60.0, 4.0); idx = int(beats / division); phase = beats / division - idx
        if idx != slice_idx:
            slice_idx = idx; slice_counter += 1; sstart = f
            stut = mode == 2 and rng.random() < P['ParamB']
            if mode == 8 and rng.random() < P['ParamB']: refresh = True
            loop = max(2, round(2 + P['ParamA']**2 * (max(2, valid // 2) - 2)))
        sa = 0.0
        if stut and mode == 2:
            k = (f - sstart) % loop; src = sstart - loop + k; sa = float(f - src)
        U = dict(InputTexture=0, History=1, State=2, MaxUV=(1.0, 1.0), Resolution=(float(W), float(H)), StateRes=(float(W), float(H)),
                 Mode=mode, Head=head, Frames=N, Valid=valid, Time=t, FrameCount=float(f), StutterAge=sa, Slice=float(slice_counter),
                 SliceAge=float(min(f - sstart, valid - 1)), BeatPhase=phase, Refresh=1.0 if refresh else 0.0)
        U.update(P)
        if mode in STATEFUL:
            refresh_now = U['Refresh']; refresh = False
            for _ in range(8 if mode == 10 else 1):
                dst = 1 - cur; fbo_state[dst].use(); inp.use(0); hist.use(1); state[cur].use(2)
                setu(ps, **U); setu(ps, Refresh=refresh_now); vs.render(moderngl.TRIANGLE_STRIP); cur = dst
        fbo_out.use(); inp.use(0); hist.use(1); state[cur].use(2)
        setu(pm, **U); vm.render(moderngl.TRIANGLE_STRIP)
        if f >= warm:
            a = np.frombuffer(fbo_out.read(components=4), np.uint8).reshape(H, W, 4)[::-1]
            imgs.append(Image.fromarray(a[:, :, :3].copy()))
    return imgs
