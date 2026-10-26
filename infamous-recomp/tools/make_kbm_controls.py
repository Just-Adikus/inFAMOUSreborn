"""Keyboard/mouse version of the Controls help screen picture (controller_2D_top_back.psd, oid 101629 in core.xpp:
512x512 DXT5, the pad seen from the back on top and from the front below). The screen's callout lines and dots are
drawn by the game at fixed points of this texture, so every key is drawn where the pad button was. Bindings =
cellPad.c: WASD move, mouse camera, LMB = R1, RMB = L1, Q = L2, R = R2, V = L3, Space/C/F/E = Cross/Circle/Square/
Triangle, Ctrl = d-pad up, arrows = d-pad, Tab = Select, Esc = Start.
Appends one texture to an existing kbm_prompts.bin ("IFKBMIC2"; the port swaps it in when keyboard prompts are on).
usage: python make_kbm_controls.py <core.xpp> <kbm_prompts.bin> [preview.png]   (KBM_LANG=russian: Russian key names)"""
import sys, os, struct, numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kbm_glyphs_draw as G
from kbm_glyphs_dxt import dxt5_decode, dxt5_encode, tex_csum
OID, W, H = 101629, 512, 512
d = open(sys.argv[1], 'rb').read()
assert d[:4] == b'PACK'
BASE = struct.unpack('>I', d[0x28:0x2C])[0]
def tex(oid):
    k = 0
    while True:
        k = d.find(struct.pack('>I', oid), k + 1)
        if k < 0: raise SystemExit('texture %d not found' % oid)
        w, h = struct.unpack('>II', d[k+4:k+12])
        if (w, h) == (W, H):
            off, fmt = struct.unpack('>II', d[k+0x20:k+0x28])
            assert (fmt >> 8) & 0xFF == 0x88
            return d[BASE+off:BASE+off+W*H]
raw = tex(OID)
lang = (os.environ.get('KBM_LANG') or os.environ.get('PS3_LANG') or '').lower()
ru = lang.startswith('ru') or lang == '8'
SPACE = 'ПРОБЕЛ' if ru else 'SPACE'
S = G.S = 8
dr = None
def box(cx, cy, w, h): return (cx-w/2)*S, (cy-h/2)*S, (cx+w/2)*S, (cy+h/2)*S
def panel(x0, y0, x1, y1):
    dr.rounded_rectangle([x0*S, y0*S, x1*S, y1*S], radius=18*S, fill=(16, 18, 23, 225), outline=(70, 76, 88, 255), width=2*S)
def key(cx, cy, w, h, t, sc=0.62): G.keycap(dr, *box(cx, cy, w, h), t, sc)
def mouse(cx, cy, w, h, hl): G.mouse(dr, *box(cx, cy, w, h), hl)
def picture(aim):
    """aim = the mouse-aim variant (struggle QTEs): left stick = mouse, Cross = left click"""
    global dr
    img = Image.new('RGBA', (W*S, H*S), (0, 0, 0, 0)); dr = ImageDraw.Draw(img)
    panel(40, 22, 472, 205)                        # back view: shoulder buttons
    panel(40, 250, 472, 478)                       # front view
    key(120, 68, 58, 46, 'Q')                      # L2  absorb
    mouse(120, 136, 70, 74, 'R')                   # L1  aim
    key(194, 180, 46, 38, 'V')                     # L3  radar
    key(398, 68, 58, 46, 'R')                      # R2  polarity wall
    mouse(398, 136, 70, 74, 'L')                   # R1  interact / shoot
    key(318, 180, 64, 38, 'Shift', 0.5)            # (run)
    key(128, 316, 58, 30, 'Ctrl', 0.55)            # up    precision
    key(96, 349, 34, 30, '\u2190')                 # left  arc restraint
    key(160, 349, 34, 30, '\u2192')                # right
    key(128, 382, 34, 30, '\u2193')                # down  lightning storm
    if aim: mouse(196, 424, 70, 76, 'move')        # left stick = the mouse
    else:   G.cluster(dr, *box(196, 426, 100, 64), ['W', 'A', 'S', 'D'])   # left stick  move
    if not aim: mouse(306, 428, 72, 80, 'move')    # right stick camera
    key(384, 304, 40, 36, 'E')                     # triangle  interact with world
    key(346, 342, 40, 36, 'F')                     # square    melee
    key(422, 342, 40, 36, 'C')                     # circle    dodge / drop / cover
    if aim: mouse(384, 388, 44, 48, 'L')           # cross = left click
    else:   key(384, 384, 78, 34, SPACE, 0.5)      # cross     jump
    key(219, 330, 42, 26, 'Tab', 0.55)             # select
    key(294, 330, 42, 26, 'Esc', 0.55)             # start
    return np.array(img.resize((W, H), Image.LANCZOS))
ALT = 0x100   # fmt bit: mouse-aim variant (the renderer uses it while the mouse drives the left stick)
def entry(w, h, raw, img, alt):
    enc = dxt5_encode(np.ascontiguousarray(img[::-1])); assert len(enc) == len(raw)
    return struct.pack('<5I', w, h, 0x88 | (ALT if alt else 0), len(raw), tex_csum(raw, len(raw))) + raw + enc, enc
new_entries = []
e, enc_norm = entry(W, H, raw, picture(False), False); new_entries.append(e)
e, enc_aim = entry(W, H, raw, picture(True), True); new_entries.append(e)
out = sys.argv[2]; assets = os.path.dirname(os.path.abspath(out))
# mouse-aim variants of the prompt glyph atlas (kbm_icons.bin): left stick -> mouse, Cross -> left click
try:
    ki = open(os.path.join(assets, 'kbm_icons.bin'), 'rb').read()
    if ki[:8] == b'IFKBMIC1':
        iw, ih, ifm, inb, ics = struct.unpack('<5I', ki[8:28]); iorig = ki[28:28+inb]; ikbm = ki[28+inb:28+2*inb]
        vis = dxt5_decode(ikbm, iw, ih)[::-1].copy()
        for code, kind, arg in ((0xF08A, 'mouse', 'move'), (0xF083, 'mouse', 'L')):
            x, y, w, h = G.CELLS[code]
            im = Image.new('RGBA', (w*S, h*S), (0, 0, 0, 0)); dr = ImageDraw.Draw(im)
            m = 2*S; G.mouse(dr, m, m, w*S-m, h*S-m, arg)
            vis[y:y+h, x:x+w] = np.array(im.resize((w, h), Image.LANCZOS))
        e, _ = entry(iw, ih, iorig, vis, True); new_entries.append(e)
        print('mouse-aim glyph atlas added')
except Exception as ex: print('(no mouse-aim glyph atlas:', ex, ')')
# mouse-aim variants of the Cross prompt buttons (round button + coloured symbol): "LMB"
LMB = '\u041b\u041a\u041c' if ru else 'LMB'
try:
    src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'make_kbm_prompts.py'), encoding='utf-8').read()
    src = src[:src.index('JOBS = [')]
    argv = sys.argv; sys.argv = [argv[0], argv[1], '-']; ns = {'__name__': 'kbmp', '__file__': os.path.join(os.path.dirname(os.path.abspath(__file__)), 'make_kbm_prompts.py')}
    exec(compile(src, 'make_kbm_prompts.py', 'exec'), ns); sys.argv = argv
    for oid, fn in ((114719, ns['round_button']), (101272, ns['symbol'])):
        w, h, r0 = ns['tex'](oid)
        vis = dxt5_decode(r0, w, h)[::-1]
        e, _ = entry(w, h, r0, fn(vis.copy(), LMB), True); new_entries.append(e)
    print('mouse-aim Cross prompts added')
except Exception as ex: print('(no mouse-aim Cross prompts:', ex, ')')
b = open(out, 'rb').read() if os.path.isfile(out) else b'IFKBMIC2' + struct.pack('<I', 0)
assert b[:8] == b'IFKBMIC2'
n = struct.unpack('<I', b[8:12])[0]
keys = {(struct.unpack('<5I', e[:20])[0], struct.unpack('<5I', e[:20])[1], struct.unpack('<5I', e[:20])[2], struct.unpack('<5I', e[:20])[4]) for e in new_entries}
p, ents = 12, []
for _ in range(n):
    w, h, f, nb, cs = struct.unpack('<5I', b[p:p+20]); e = b[p:p+20+2*nb]; p += 20 + 2*nb
    if (w, h, f, cs) not in keys: ents.append(e)
ents += new_entries
open(out, 'wb').write(b'IFKBMIC2' + struct.pack('<I', len(ents)) + b''.join(ents))
print('wrote', out, len(ents), 'textures (controls screen + mouse-aim variants)')
if len(sys.argv) > 3:
    ims = [dxt5_decode(raw, W, H)[::-1], dxt5_decode(enc_norm, W, H)[::-1], dxt5_decode(enc_aim, W, H)[::-1]]
    c = Image.new('RGB', (W*3 + 20, H), (215, 215, 215))
    for i, im in enumerate(ims):
        bg = np.full((H, W, 3), 215.0); a = im[..., 3:4] / 255.0
        c.paste(Image.fromarray((im[..., :3]*a + bg*(1-a)).astype(np.uint8)), (i*(W+10), 0))
    c.save(sys.argv[3])
