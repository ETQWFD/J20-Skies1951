from PIL import Image, ImageDraw
import math, os
SIZES={'ldpi':36,'mdpi':48,'hdpi':72,'xhdpi':96,'xxhdpi':144}
RED=(222,20,26); YEL=(255,238,70)
def star(cx,cy,rOut,rIn,rot):
    pts=[]
    for i in range(10):
        r=rOut if i%2==0 else rIn
        a=math.radians(rot+i*36)
        pts.append((cx+r*math.cos(a), cy+r*math.sin(a)))
    return pts
B=(5.0,5.0,3.0)
SML=[(10.0,2.0),(12.0,4.0),(12.0,7.0),(10.0,9.0)]
for name,S in SIZES.items():
    icon=Image.new('RGB',(S,S),RED)
    W=S; H=round(S*20/30)
    if H<1:H=1
    flag=Image.new('RGB',(W,H),RED)
    d=ImageDraw.Draw(flag)
    k=W/30.0
    bx,by=B[0]*k,B[1]*k; br=B[2]*k
    d.polygon(star(bx,by,br,br*0.405,-90),fill=YEL)
    for (u,v) in SML:
        cx,cy=u*k,v*k; rr=1.0*k
        ang=math.degrees(math.atan2(by-cy,bx-cx))
        d.polygon(star(cx,cy,rr,rr*0.405,ang-90),fill=YEL)
    icon.paste(flag,(0,(S-H)//2))
    os.makedirs(f'android/apk/res/drawable-{name}',exist_ok=True)
    icon.save(f'android/apk/res/drawable-{name}/icon.png')
print('icons done')
