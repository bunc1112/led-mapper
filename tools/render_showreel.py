exec(open(__import__('os').path.join(__import__('os').path.dirname(__file__),'preview_harness.py')).read())
import imageio
from PIL import ImageDraw, ImageFont
lab=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',15)
cases=[("Original",-1,{}),
 ("0 Slit-Scan  · Left>Right",0,dict(TimeMap=0)),
 ("0 Slit-Scan  · Spiral",0,dict(TimeMap=3,ParamA=.3,ParamB=.2)),
 ("0 Slit-Scan  · Luma",0,dict(TimeMap=5)),
 ("1 Chroma Time Split",1,dict(ParamA=.3,Depth=.25)),
 ("2 Beat Stutter",2,dict(ParamA=.4,ParamB=.6)),
 ("3 Chrono Trails",3,dict(ParamA=.7,ParamB=.5,Depth=.6)),
 ("4 Rutt-Etra (time-sliced)",4,dict(ParamA=.45,ParamB=.25,Depth=.4)),
 ("5 Time Mosaic",5,dict(ParamA=.35,ParamB=.45)),
 ("6 Long Exposure · Light Paint",6,dict(ParamA=.85,ParamB=.3,Depth=.5)),
 ("7 Time Tunnel",7,dict(ParamA=.35,ParamB=.12,Depth=.9))]
F=75; sheet=[]; clips=[]
for name,m,p in cases:
    if m<0:
        ims=[Image.fromarray(frame((64+f)/25)[::-1][:,:,:3].copy()) for f in range(F)]
    else:
        ims=run(m,p,frames=F,stutter=(m==2))
    for im in ims:
        d=ImageDraw.Draw(im); d.rectangle([0,H-24,W,H],fill=(0,0,0)); d.text((8,H-21),name,fill=(255,255,255),font=lab)
    clips.append(ims); print(name,flush=True)
# grid video 4x3 (last cell = title)
title=Image.new('RGB',(W,H),(14,10,30)); d=ImageDraw.Draw(title)
big=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',34)
d.text((30,60),"CHRONOS",fill=(255,200,40),font=big); d.text((30,110),"8 time effects · 1 FFGL plugin",fill=(220,220,255),font=lab); d.text((30,135),"for Resolume Arena / Avenue",fill=(160,160,200),font=lab)
w=imageio.get_writer('chronos_showreel.mp4',fps=25,codec='libx264',quality=8,macro_block_size=8,ffmpeg_params=['-preset','veryfast','-pix_fmt','yuv420p'])
for f in range(F):
    g=Image.new('RGB',(W*4,H*3))
    cells=[c[f] for c in clips]+[title]
    for i,c in enumerate(cells): g.paste(c,((i%4)*W,(i//4)*H))
    w.append_data(np.array(g))
    if f==50: g.save('chronos_contact_sheet.png')
w.close()
