import re, math, sys, numpy as np, moderngl
from PIL import Image, ImageDraw, ImageFont
import os
src=open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','source','Chronos','Shaders.h')).read()
sh={k:re.search(k+r'\[\] = R"\((.*?)\)";',src,re.S).group(1) for k in ['vertexShader','copyShader','mainShader']}
W,H=384,216; N=64
ctx=moderngl.create_standalone_context(backend='egl',require=410)
quad=ctx.buffer(np.array([-1,-1,0,1,0,0, 1,-1,0,1,1,0, -1,1,0,1,0,1, 1,1,0,1,1,1],'f4'))
def prog(fs):
    p=ctx.program(vertex_shader=sh['vertexShader'],fragment_shader=fs)
    return p, ctx.vertex_array(p,[(quad,'4f 2f','vPosition','vUV')])
pc,vc=prog(sh['copyShader']); pm,vm=prog(sh['mainShader'])
hist=ctx.texture_array((W,H,N),4); hist.filter=(moderngl.LINEAR,moderngl.LINEAR)
inp=ctx.texture((W,H),4); out=ctx.texture((W,H),4); fbo_out=ctx.framebuffer([out])
try: font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',40)
except: font=ImageFont.load_default()
def frame(t):
    im=Image.new('RGB',(W,H),(8,8,20)); d=ImageDraw.Draw(im)
    for i in range(7):
        x=W/2+math.sin(t*1.3+i)*W*0.38; y=H/2+math.cos(t*0.9+i*1.7)*H*0.32; r=14+8*math.sin(t*2+i)
        c=tuple(int(127+127*math.sin(i*0.9+k*2.1)) for k in range(3)); d.ellipse([x-r,y-r,x+r,y+r],fill=c)
    # dancer-ish swinging bar
    a=math.sin(t*2.2)*1.1; cx,cy=W*0.5,H*0.55
    d.line([cx,cy,cx+math.sin(a)*90,cy-math.cos(a)*90],fill=(255,255,255),width=10)
    d.text((W*0.5-100+math.sin(t)*60,10),"CHRONOS",fill=(255,200,40),font=font)
    a=np.array(im.convert('RGBA'))[::-1].copy(); return a
def setu(p,**k):
    for n,v in k.items():
        if n in p: p[n].value=v
def run(mode,params,frames=100,fps=25,warm=64,stutter=False):
    imgs=[]; head=-1; valid=0
    rng=np.random.default_rng(3); slice_=0; st=False; sstart=0; loop=8
    for f in range(warm+frames):
        t=f/fps; inp.write(frame(t).tobytes())
        head=(head+1)%N; valid=min(valid+1,N)
        # render into array layer
        fbl=ctx.framebuffer(color_attachments=[ctx.texture((W,H),4)])
        fbl.use(); inp.use(0); setu(pc,InputTexture=0,MaxUV=(1.0,1.0)); vc.render(moderngl.TRIANGLE_STRIP)
        hist.write(fbl.read(components=4),viewport=(0,0,head,W,H,1)); fbl.color_attachments[0].release(); fbl.release()
        beat=t*128/60; idx=int(beat/0.5)
        sa=0.0
        if stutter:
            if idx!=slice_:
                slice_=idx; st=rng.random()<params.get('ParamB',.5); sstart=f; loop=max(2,round(2+params['ParamA']**2*(valid//2-2)))
            if st: k=(f-sstart)%loop; src=sstart-loop+k; sa=float(f-src)
        fbo_out.use(); inp.use(0); hist.use(1)
        base=dict(InputTexture=0,History=1,MaxUV=(1.0,1.0),Resolution=(float(W),float(H)),Mode=mode,TimeMap=0,Head=head,Frames=N,Valid=valid,
                  Depth=.8,ParamA=0.,ParamB=.5,Reverse=0.,Mix=1.,Time=t,StutterAge=sa,Slice=float(int(beat/1.0)))
        base.update(params); setu(pm,**base); vm.render(moderngl.TRIANGLE_STRIP)
        if f>=warm:
            a=np.frombuffer(fbo_out.read(components=4),np.uint8).reshape(H,W,4)[::-1]
            imgs.append(Image.fromarray(a[:,:,:3].copy()))
    return imgs
