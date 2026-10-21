"""Distort Me logo generator.

Needs fonttools, numpy and Pillow, plus the fonts as .ttf in DM_FONT_DIR
(default tools/logo/fonts), e.g. big-shoulders-display-900.ttf converted from
@fontsource/big-shoulders-display.
Final logo: logo.make(cols=28, me_rows=6, gap_cells=1, me_gap_cols=1, m_cols=15)

Distort Me logo: DISTORT in Space Mono Bold, ME drawn as CP437 shade
blocks (░▒▓█) on a grid that spans DISTORT's exact ink width."""
import numpy as np, os, sys
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.boundsPen import BoundsPen
from PIL import Image, ImageDraw, ImageFont
OUT=os.path.dirname(os.path.abspath(__file__))
FD=os.environ.get('DM_FONT_DIR', os.path.join(OUT, 'fonts')) + '/'  # folder with the .ttf files
FONT=FD+'big-shoulders-display-900.ttf'
def use(path):
    global FONT,f,gs,cmap,upm
    FONT=path;f=TTFont(path);gs=f.getGlyphSet();cmap=f.getBestCmap();upm=f['head'].unitsPerEm
use(FONT)
def glyph_bounds(ch):
    bp=BoundsPen(gs);gs[cmap[ord(ch)]].draw(bp);return bp.bounds
def make(fg='#ece9e2', cols=28, me_rows=None, gap_cells=1.0, track=0.0, W=280.0, pad=0, me_gap_cols=None, run_out=0.0, m_cols=None):
    """me_gap_cols: columns between M and E (None = the font's own spacing).
    run_out: how many cells DISTORT runs past the ME grid on the right, by
    opening up its letter spacing (the left edges stay aligned)."""
    # DISTORT ink width -> scale
    adv_of=lambda ch: gs[cmap[ord(ch)]].width
    def ink_w(s):
        x=0;xs=[]
        for ch in 'DISTORT':
            b=glyph_bounds(ch);xs+=[x+b[0]*s,x+b[2]*s];x+=adv_of(ch)*s+track
        return min(xs),max(xs)
    l,r=ink_w(1);s=W/(r-l)
    for _ in range(3):
        l,r=ink_w(s);s*=W/(r-l)
    l,r=ink_w(s)
    if run_out:
        track=run_out*(W/cols)/6.0  # 6 gaps between the 7 letters
        l,r=ink_w(s)
    capb=[glyph_bounds(c) for c in 'DISTORT'];top=max(b[3] for b in capb)*s;bot=min(b[1] for b in capb)*s;capH=top-bot
    cell=W/cols
    rows=me_rows or max(3,round(capH/cell))
    gap=gap_cells*cell
    out=[]
    x=-l;cmds=[]
    for ch in 'DISTORT':
        pen=SVGPathPen(gs);gs[cmap[ord(ch)]].draw(TransformPen(pen,(s,0,0,-s,x,top)));cmds.append(pen.getCommands());x+=adv_of(ch)*s+track
    out.append(f'<path fill="{fg}" d="{" ".join(cmds)}"/>')
    # ME mask from the same font
    fnt=ImageFont.truetype(FONT,800)
    def mask(text):
        im=Image.new('L',(2600,1300));ImageDraw.Draw(im).text((20,20),text,font=fnt,fill=255)
        a=np.array(im)>127;yy,xx=np.nonzero(a);return a[yy.min():yy.max()+1,xx.min():xx.max()+1].astype(float)
    if me_gap_cols is None:
        parts=[(mask('ME'),0,cols)]
    else:
        mM,mE=mask('M'),mask('E');avail=cols-me_gap_cols
        cm=m_cols or round(avail*mM.shape[1]/(mM.shape[1]+mE.shape[1]))
        parts=[(mM,0,cm),(mE,cm+me_gap_cols,cols)]
    Y2=capH+gap
    # coverage per cell, quantised to 0, 1/4, 1/2, 3/4, 1
    lv=np.zeros((rows,cols),int)
    for M,c0,c1 in parts:
      mh,mw=M.shape;n=c1-c0
      for rr in range(rows):
        for c in range(c0,c1):
            y0=int(rr/rows*mh);y1=max(y0+1,int((rr+1)/rows*mh));x0=int((c-c0)/n*mw);x1=max(x0+1,int((c-c0+1)/n*mw))
            cov=M[y0:y1,x0:x1].mean()
            lv[rr,c]=0 if cov<0.12 else 4 if cov>0.88 else 1 if cov<0.38 else 2 if cov<0.62 else 3
    p=cell/4
    pats={1:[(0,0),(2,1),(0,2),(2,3)],2:[(0,0),(2,0),(1,1),(3,1),(0,2),(2,2),(1,3),(3,3)]}
    pats[3]=[(i,j) for i in range(4) for j in range(4) if (i,j) not in pats[1]]
    defs=[]
    for k,pts in pats.items():
        rects=''.join(f'<rect x="{i*p:.3f}" y="{j*p:.3f}" width="{p:.3f}" height="{p:.3f}"/>' for i,j in pts)
        defs.append(f'<pattern id="dm{k}" patternUnits="userSpaceOnUse" x="0" y="{Y2:.3f}" width="{cell:.3f}" height="{cell*rows/rows:.3f}"><g fill="{fg}">{rects}</g></pattern>')
    ch_=(rows and (capH*0+cell))  # square cells
    paths={1:[],2:[],3:[],4:[]}
    for rr in range(rows):
        c=0
        while c<cols:
            k=lv[rr,c]
            if k==0: c+=1;continue
            e=c
            while e<cols and lv[rr,e]==k: e+=1
            x0=c*cell;y0=Y2+rr*cell
            if k==4:
                paths[4].append(f'M{x0:.3f} {y0:.3f}h{(e-c)*cell:.3f}v{cell:.3f}h{-(e-c)*cell:.3f}z')
            else:
                # shade cells as explicit dots (no <pattern>: Photoshop's UXP can't draw them)
                for cc in range(c,e):
                    for i,j in pats[k]:
                        paths[4].append(f'M{cc*cell+i*p:.3f} {y0+j*p:.3f}h{p:.3f}v{p:.3f}h{-p:.3f}z')
            c=e
    # one path per level: no anti-aliasing seams between neighbouring cells
    for k in (1,2,3,4):
        if paths[k]:
            out.append(f'<path fill="{fg}" d="{"".join(paths[k])}"/>')
    H=Y2+rows*cell;Wt=max(W,r-l)
    svg=(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{-pad} {-pad} {Wt+2*pad:.2f} {H+2*pad:.2f}" width="{Wt+2*pad:.1f}" height="{H+2*pad:.1f}">'
         f'{"".join(out)}</svg>')
    return svg,(W,H,cell,rows,capH)
