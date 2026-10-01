#!/usr/bin/env python3
# Build the embedded gamefont.ttf:
#   base gamefont (Latin + simplified Chinese)  <- keep its look
# + DotGothic16  (Japanese kana + JIS kanji)
# + DejaVu Sans  (Cyrillic)
# + Droid Sans Fallback Full (remaining CJK simplified chars)
# All donors are normalised to the base units-per-em, subset to ONLY the
# codepoints the base lacks, then merged. Output replaces gamefont.ttf.
import glob, os, subprocess, sys
from fontTools.ttLib import TTFont
from fontTools.subset import Subsetter, Options
from fontTools.ttLib.scaleUpem import scale_upem

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
TMP = "build/fontmerge"
os.makedirs(TMP, exist_ok=True)

# 1) regenerate the codepoint list + font_cps.h
subprocess.run([sys.executable, "tools/genfont.py"], check=True)

base_path = "third_party/font/gamefont.ttf"
dot_path  = "third_party/raylib/examples/text/resources/DotGothic16-Regular.ttf"
dej_path  = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
dro_path  = "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"

def scan_needed():
    need=set(range(0x20,0x7F))
    for ch in "·—…、：；？！，。“”‘’《》①②③④⑤⑥⑦⑧⑨⑩←→↑↓√×／":
        need.add(ord(ch))
    need.update(range(0x400,0x500)); need.update(range(0x3040,0x3100)); need.update(range(0x31F0,0x3200))
    for fn in glob.glob("src/*.c")+glob.glob("src/*.h"):
        for ch in open(fn,encoding="utf-8").read():
            o=ord(ch)
            if o>=0x4E00 or 0x3000<=o<=0x303F or 0xFF00<=o<=0xFFEF or 0x400<=o<=0x4FF \
               or 0x3040<=o<=0x30FF or 0x31F0<=o<=0x31FF:
                need.add(o)
    # drop unassigned/non-character slots the block ranges pull in
    need={c for c in need if not (0x3040==c or c in (0x3097,0x3098,0x309f,0x30A0,0x30FF)
                                  or 0x31F0<=c<=0x31FF)}
    return need

need=scan_needed()
base=TTFont(base_path); upem=base["head"].unitsPerEm
base_c=set(base.getBestCmap().keys())

def scaled(path):
    f=TTFont(path)
    if f["head"].unitsPerEm!=upem: scale_upem(f,upem)
    return f

donors=[(dot_path,0), (dej_path,0), (dro_path,0)]
# assign missing cps to the first donor that has them, in priority order
missing=sorted(c for c in need if c not in base_c and c!=0x3040)
assigned=[set() for _ in donors]
fonts=[]
for path,_ in donors:
    fonts.append(scaled(path))
covered=set()
for i,f in enumerate(fonts):
    cm=set(f.getBestCmap().keys())
    for c in missing:
        if c not in covered and c in cm:
            assigned[i].add(c); covered.add(c)
unresolved=[c for c in missing if c not in covered]
print("upem",upem,"needed",len(need),"missing",len(missing))
for (path,_),a in zip(donors,assigned): print(" donor",os.path.basename(path),len(a))
print("unresolved",len(unresolved),[hex(c) for c in unresolved][:50])

def subset(font,cps,out):
    if not cps: return
    o=Options(); o.glyph_names=False; o.recalc_bounds=True
    o.notdef_outline=True; o.name_IDs=[]; o.layout_features=[]
    o.drop_tables += ["MATH","MATH  ","GSUB","GPOS","GDEF","kern","vhea","vmtx",
                      "hdmx","LTSH","VDMX","BASE","JSTF","DSIG","COLR","CPAL","SVG "]
    s=Subsetter(options=o); s.populate(unicodes=sorted(cps)); s.subset(font)
    font.save(out)

subset(base, sorted(need & base_c), f"{TMP}/a_base.ttf")
subset(fonts[0], assigned[0], f"{TMP}/b_dot.ttf")
subset(fonts[1], assigned[1], f"{TMP}/c_dej.ttf")
subset(fonts[2], assigned[2], f"{TMP}/d_dro.ttf")

# 2) merge in priority order (base first keeps its glyphs on shared unicode)
parts=[f"{TMP}/a_base.ttf"]
for p in (f"{TMP}/b_dot.ttf",f"{TMP}/c_dej.ttf",f"{TMP}/d_dro.ttf"):
    if os.path.exists(p): parts.append(p)
from fontTools.merge import Merger
merged=Merger().merge(parts)

mc=set(merged.getBestCmap().keys())
bad=[c for c in need if c not in mc and c!=0x3040]
print("merged glyphs",len(merged.getGlyphOrder()),"coverage gaps:",len(bad),[hex(c) for c in bad][:30])

out="third_party/font/gamefont.ttf"
merged.save(out)
print("wrote",out,os.path.getsize(out),"bytes")
