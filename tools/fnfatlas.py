"""Image and atlas handling for the FunkinWii asset converter.

Every sprite source the game uses (Sparrow sheets, Packer sheets, Adobe
Animate texture atlases and plain images) is turned into the same thing: a
list of frames, each a trimmed premultiplied RGBA image with its offset
inside the untrimmed frame, packed into 1024x1024 GX texture pages.

Sizes inside the atlas description stay in the game's own units; only the
pixels are scaled down, so the engine keeps the original game's coordinates.
"""

import hashlib
import json
import math
import os
import re
import struct
import xml.etree.ElementTree as ET

from PIL import Image, ImageChops

PAGE = 1024
PAD = 2

GX_TF_I8 = 0x1
GX_TF_IA8 = 0x3
GX_TF_RGB5A3 = 0x5
GX_TF_RGBA8 = 0x6


# ---------------------------------------------------------------- textures

def encode_rgb5a3(img):
    """img: premultiplied 'RGBA' bytes image (mode RGBA holding premultiplied data)."""
    w, h = img.size
    tw, th = (w + 3) & ~3, (h + 3) & ~3
    src = img.tobytes()
    out = bytearray(tw * th * 2)
    o = 0
    for ty in range(0, th, 4):
        for tx in range(0, tw, 4):
            for y in range(ty, ty + 4):
                for x in range(tx, tx + 4):
                    if x < w and y < h:
                        i = (y * w + x) * 4
                        r, g, b, a = src[i], src[i + 1], src[i + 2], src[i + 3]
                        if a >= 0xE0:
                            v = 0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
                        else:
                            v = ((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)
                        out[o] = v >> 8
                        out[o + 1] = v & 0xFF
                    o += 2
    return tw, th, bytes(out)


def encode_rgba8(img):
    w, h = img.size
    tw, th = (w + 3) & ~3, (h + 3) & ~3
    src = img.tobytes()
    out = bytearray(tw * th * 4)
    o = 0
    for ty in range(0, th, 4):
        for tx in range(0, tw, 4):
            ar = bytearray(32)
            gb = bytearray(32)
            k = 0
            for y in range(ty, ty + 4):
                for x in range(tx, tx + 4):
                    if x < w and y < h:
                        i = (y * w + x) * 4
                        ar[k * 2] = src[i + 3]
                        ar[k * 2 + 1] = src[i]
                        gb[k * 2] = src[i + 1]
                        gb[k * 2 + 1] = src[i + 2]
                    k += 1
            out[o:o + 32] = ar
            out[o + 32:o + 64] = gb
            o += 64
    return tw, th, bytes(out)


def premultiply(img):
    """RGBA straight alpha -> RGBA image whose bytes are premultiplied."""
    img = img.convert("RGBA")
    pm = img.convert("RGBa")
    return Image.frombytes("RGBA", pm.size, pm.tobytes())


def write_tex(path, img_pm, fmt=GX_TF_RGB5A3, logical=None):
    """img_pm: RGBA image already premultiplied."""
    if fmt == GX_TF_RGBA8:
        tw, th, data = encode_rgba8(img_pm)
    else:
        tw, th, data = encode_rgb5a3(img_pm)
    lw, lh = logical if logical else img_pm.size
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        hdr = b"FTX1" + struct.pack(">HHB3xI", tw, th, fmt, len(data))
        hdr += struct.pack(">HHHH", img_pm.size[0], img_pm.size[1], int(round(lw)) & 0xFFFF, int(round(lh)) & 0xFFFF)
        hdr += b"\0" * (32 - len(hdr))
        f.write(hdr)
        f.write(data)


# ---------------------------------------------------------------- frames

class Frame:
    __slots__ = ("name", "img", "ox", "oy", "w", "h", "fw", "fh", "key")

    def __init__(self, name, img, ox, oy, w, h, fw, fh):
        self.name = name
        self.img = img      # premultiplied RGBA at atlas scale, or None for empty
        self.ox, self.oy = ox, oy    # logical offset of the trimmed image in the frame
        self.w, self.h = w, h        # logical size of the trimmed image
        self.fw, self.fh = fw, fh    # logical size of the whole frame
        self.key = None


def scale_image(img, scale):
    """Premultiplied downscale of a straight-alpha RGBA image; returns premultiplied RGBA."""
    pm = img.convert("RGBa")
    if scale != 1.0:
        nw = max(1, int(round(img.size[0] * scale)))
        nh = max(1, int(round(img.size[1] * scale)))
        pm = pm.resize((nw, nh), Image.LANCZOS if scale < 1 else Image.NEAREST)
    return Image.frombytes("RGBA", pm.size, pm.tobytes())


def trim_pm(img):
    """Returns (bbox) of non-transparent pixels of an RGBA image, or None."""
    return img.getchannel("A").getbbox()


# ---------------------------------------------------------------- Sparrow / Packer

def load_sparrow(png, xml, scale, nearest=False):
    sheet = Image.open(png).convert("RGBA")
    text = open(xml, "rb").read().decode("utf-8-sig", "replace")
    frames = []
    cache = {}
    # Regex rather than an XML parser: some sheets have raw "<" or "&" in names.
    for m in re.finditer(r"<SubTexture\s(.*?)/>", text, re.S):
        a = {k: _xml_unescape(v) for k, v in re.findall(r'(\w+)\s*=\s*"([^"]*)"', m.group(1))}
        if "x" not in a or "width" not in a:
            continue
        name = a.get("name", "")
        x, y = int(float(a["x"])), int(float(a["y"]))
        w, h = int(float(a["width"])), int(float(a["height"]))
        rot = a.get("rotated", "false") == "true"
        fx = float(a.get("frameX", 0))
        fy = float(a.get("frameY", 0))
        key = (x, y, w, h, rot)
        if rot:
            region = (x, y, x + h, y + w)
        else:
            region = (x, y, x + w, y + h)
        fw = float(a.get("frameWidth", w))
        fh = float(a.get("frameHeight", h))
        if w <= 0 or h <= 0:
            frames.append(Frame(name, None, 0, 0, 0, 0, fw, fh))
            continue
        if key not in cache:
            sub = sheet.crop(region)
            if rot:
                sub = sub.rotate(90, expand=True)
            cache[key] = sub
        sub = cache[key]
        frames.append(Frame(name, sub, -fx, -fy, w, h, fw, fh))
    return finish_frames(frames, scale, nearest)


def _xml_unescape(v):
    return v.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", '"').replace("&apos;", "'").replace("&amp;", "&")


def load_packer(png, txt, scale, nearest=False):
    sheet = Image.open(png).convert("RGBA")
    frames = []
    for line in open(txt, encoding="utf-8-sig"):
        if "=" not in line:
            continue
        name, rest = line.split("=", 1)
        x, y, w, h = [int(v) for v in rest.split()[:4]]
        sub = sheet.crop((x, y, x + w, y + h))
        frames.append(Frame(name.strip(), sub, 0, 0, w, h, w, h))
    return finish_frames(frames, scale, nearest)


def load_image(png, scale, nearest=False):
    img = Image.open(png).convert("RGBA")
    w, h = img.size
    return finish_frames([Frame("", img, 0, 0, w, h, w, h)], scale, nearest, trim=False)


def finish_frames(frames, scale, nearest, trim=True):
    """Trims transparent borders and scales every frame image; shares identical ones."""
    done = {}
    for fr in frames:
        if fr.img is None:
            continue
        ident = id(fr.img)
        if ident in done:
            fr.img, fr.ox, fr.oy, fr.w, fr.h = done[ident][0], fr.ox + done[ident][1], fr.oy + done[ident][2], done[ident][3], done[ident][4]
            continue
        img = fr.img
        dx = dy = 0
        lw, lh = fr.w, fr.h
        if trim:
            bb = trim_pm(img)
            if bb is None:
                done[ident] = (None, 0, 0, 0, 0)
                fr.img = None
                continue
            if bb != (0, 0, img.size[0], img.size[1]):
                sx = fr.w / img.size[0]
                sy = fr.h / img.size[1]
                img = img.crop(bb)
                dx, dy = bb[0] * sx, bb[1] * sy
                lw, lh = img.size[0] * sx, img.size[1] * sy
        if nearest:
            pm = scale_image(img, 1.0)
        else:
            pm = scale_image(img, scale)
        done[ident] = (pm, dx, dy, lw, lh)
        fr.img, fr.ox, fr.oy, fr.w, fr.h = pm, fr.ox + dx, fr.oy + dy, lw, lh
    return frames


# ---------------------------------------------------------------- Adobe Animate

def _mx(d):
    """Element matrix from either JSON dialect -> (a, b, c, d, tx, ty)."""
    if "MX" in d:
        m = d["MX"]
        return tuple(float(v) for v in m[:6])
    if "M3D" in d:
        m = d["M3D"]
        if isinstance(m, dict):
            return (m["m00"], m["m01"], m["m10"], m["m11"], m["m30"], m["m31"])
        return (m[0], m[1], m[4], m[5], m[12], m[13])
    if "Matrix3D" in d:
        m = d["Matrix3D"]
        return (m["m00"], m["m01"], m["m10"], m["m11"], m["m30"], m["m31"])
    return (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)


def _mul(p, c):
    """p after c: point -> p(c(point))."""
    a1, b1, c1, d1, x1, y1 = p
    a2, b2, c2, d2, x2, y2 = c
    return (
        a1 * a2 + c1 * b2,
        b1 * a2 + d1 * b2,
        a1 * c2 + c1 * d2,
        b1 * c2 + d1 * d2,
        a1 * x2 + c1 * y2 + x1,
        b1 * x2 + d1 * y2 + y1,
    )


def _color(c):
    """Colour effect -> (rm, gm, bm, am, ro, go, bo, ao) or None."""
    if not c:
        return None
    m = c.get("M") or c.get("mode")
    g = lambda *ks, default=0.0: next((float(c[k]) for k in ks if k in c), default)
    if m in ("AD", "Advanced"):
        return (g("RM", "RedMultiplier", "redMultiplier", default=1), g("GM", "greenMultiplier", default=1),
                g("BM", "blueMultiplier", default=1), g("AM", "alphaMultiplier", default=1),
                g("RO", "redOffset"), g("GO", "greenOffset"), g("BO", "blueOffset"), g("AO", "AlphaOffset", "alphaOffset"))
    if m in ("CA", "Alpha"):
        return (1, 1, 1, g("AM", "alphaMultiplier", default=1), 0, 0, 0, 0)
    if m in ("CBRT", "Brightness"):
        br = g("BRT", "brightness")
        mul = 1 - abs(br)
        off = br * 255 if br >= 0 else 0
        return (mul, mul, mul, 1, off, off, off, 0)
    if m in ("T", "Tint"):
        tc = c.get("TC") or c.get("tintColor") or "#000000"
        tm = g("TM", "tintMultiplier")
        r, gg, b = int(tc[1:3], 16), int(tc[3:5], 16), int(tc[5:7], 16)
        mul = 1 - tm
        return (mul, mul, mul, 1, r * tm, gg * tm, b * tm, 0)
    return None


def _ccat(parent, child):
    """Colour transform: child applied first, then parent."""
    if parent is None:
        return child
    if child is None:
        return parent
    rm, gm, bm, am, ro, go, bo, ao = child
    RM, GM, BM, AM, RO, GO, BO, AO = parent
    return (rm * RM, gm * GM, bm * BM, am * AM, ro * RM + RO, go * GM + GO, bo * BM + BO, ao * AM + AO)


class Timeline:
    def __init__(self, name, layers):
        self.name = name
        self.layers = layers  # list of dict(name, visible, frames=[(index, duration, name, elements)])
        self.frame_count = 0
        for l in layers:
            for fr in l["frames"]:
                self.frame_count = max(self.frame_count, fr[0] + fr[1])
        self.bounds = None

    def frame_at(self, layer, i):
        for fr in layer["frames"]:
            if fr[0] <= i < fr[0] + fr[1]:
                return fr
        return None


def _parse_timeline(tl, name):
    layers = []
    lj = tl.get("L") or tl.get("LAYERS") or []
    for l in lj:
        lname = l.get("LN") or l.get("Layer_name") or ""
        lt = l.get("LT") or l.get("Layer_type")
        visible = lt not in ("Clp", "Clipper")
        if lt in ("Fld", "Folder"):
            visible = False
        frames = []
        for fr in l.get("FR") or l.get("Frames") or []:
            idx = int(fr.get("I", fr.get("index", 0)))
            du = int(fr.get("DU", fr.get("duration", 1)))
            fname = fr.get("N", fr.get("name"))
            els = []
            for e in fr.get("E") or fr.get("elements") or []:
                si = e.get("SI") or e.get("SYMBOL_Instance")
                asi = e.get("ASI") or e.get("ATLAS_SPRITE_instance")
                if si is not None:
                    lp = si.get("LP", si.get("loop", "LP"))
                    loop = {"PO": 1, "playonce": 1, "SF": 2, "singleframe": 2}.get(lp, 0)
                    st = si.get("ST", si.get("symbolType", "G"))
                    els.append(("S", si.get("SN") or si.get("SYMBOL_name"), int(si.get("FF", si.get("firstFrame", 0)) or 0),
                                loop, _mx(si), _color(si.get("C") or si.get("color")), st))
                elif asi is not None:
                    els.append(("A", str(asi.get("N", asi.get("name"))), _mx(asi)))
            frames.append((idx, du, fname, els))
        layers.append({"name": lname, "visible": visible, "frames": frames})
    return Timeline(name, layers)


class AnimateAtlas:
    def __init__(self, folder):
        self.folder = folder
        d = json.load(open(os.path.join(folder, "Animation.json"), encoding="utf-8-sig"))
        an = d.get("AN") or d.get("ANIMATION")
        sd = d.get("SD") or d.get("SYMBOL_DICTIONARY") or {}
        md = d.get("MD") or d.get("metadata") or {}
        self.framerate = float(md.get("FRT", md.get("framerate", 24)) or 24)
        self.main = _parse_timeline(an.get("TL") or an.get("TIMELINE"), an.get("SN") or an.get("SYMBOL_name") or "")
        self.symbols = {}
        for s in sd.get("S") or sd.get("Symbols") or []:
            sn = s.get("SN") or s.get("SYMBOL_name")
            self.symbols[sn] = _parse_timeline(s.get("TL") or s.get("TIMELINE"), sn)
        self.sprites = {}
        self.sheets = []
        self._load_spritemaps()
        self._sprite_cache = {}

    def _load_spritemaps(self):
        n = 1
        while True:
            jp = os.path.join(self.folder, "spritemap%d.json" % n)
            if not os.path.exists(jp):
                break
            sm = json.load(open(jp, encoding="utf-8-sig"))
            meta = sm.get("meta", {})
            res = float(meta.get("resolution", 1) or 1)
            png = os.path.join(self.folder, meta.get("image", "spritemap%d.png" % n))
            if not os.path.exists(png):
                png = os.path.join(self.folder, "spritemap%d.png" % n)
            sheet = Image.open(png).convert("RGBA")
            self.sheets.append(sheet)
            for s in sm["ATLAS"]["SPRITES"]:
                sp = s["SPRITE"]
                self.sprites[str(sp["name"])] = (sheet, sp["x"], sp["y"], sp["w"], sp["h"], bool(sp.get("rotated")), res)
            n += 1

    # -- frame lookup
    def sym_frame(self, el, idx_in_keyframe):
        tl = self.symbols.get(el[1])
        if tl is None or tl.frame_count == 0:
            return None, 0
        ff, loop = el[2], el[3]
        f = ff + idx_in_keyframe
        if loop == 0:
            f %= tl.frame_count
        elif loop == 1:
            f = min(f, tl.frame_count - 1)
        else:
            f = ff
        if f < 0:
            f = 0
        return tl, f

    def collect(self, tl, i, mtx, col, out, depth=0):
        """Appends (sprite_name, matrix, colour) for frame i of timeline tl."""
        if depth > 32:
            return
        for layer in reversed(tl.layers):
            if not layer["visible"]:
                continue
            fr = tl.frame_at(layer, i)
            if fr is None:
                continue
            for el in fr[3]:
                if el[0] == "A":
                    out.append((el[1], _mul(mtx, el[2]), col))
                else:
                    sub, f = self.sym_frame(el, i - fr[0])
                    if sub is None:
                        continue
                    c = _ccat(col, el[5])
                    if c is not None and c[3] <= 0 and c[7] <= 0:
                        continue
                    self.collect(sub, f, _mul(mtx, el[4]), c, out, depth + 1)

    def sprite_rect(self, name):
        sp = self.sprites.get(name)
        if not sp:
            return None
        sheet, x, y, w, h, rot, res = sp
        return w / res, h / res

    def frame_bounds(self, tl, i):
        items = []
        self.collect(tl, i, (1, 0, 0, 1, 0, 0), None, items)
        bb = None
        for name, m, col in items:
            r = self.sprite_rect(name)
            if r is None:
                continue
            w, h = r
            for (px, py) in ((0, 0), (w, 0), (0, h), (w, h)):
                X = m[0] * px + m[2] * py + m[4]
                Y = m[1] * px + m[3] * py + m[5]
                if bb is None:
                    bb = [X, Y, X, Y]
                else:
                    bb[0] = min(bb[0], X)
                    bb[1] = min(bb[1], Y)
                    bb[2] = max(bb[2], X)
                    bb[3] = max(bb[3], Y)
        return bb, items

    def timeline_bounds(self, tl):
        if tl.bounds is None:
            bb = None
            for i in range(tl.frame_count):
                b, _ = self.frame_bounds(tl, i)
                if b is None:
                    continue
                if bb is None:
                    bb = list(b)
                else:
                    bb = [min(bb[0], b[0]), min(bb[1], b[1]), max(bb[2], b[2]), max(bb[3], b[3])]
            tl.bounds = bb or [0, 0, 1, 1]
        return tl.bounds

    def _sprite_img(self, name, scale):
        key = (name, scale)
        if key not in self._sprite_cache:
            sheet, x, y, w, h, rot, res = self.sprites[name]
            if rot:
                img = sheet.crop((x, y, x + h, y + w)).rotate(90, expand=True)
            else:
                img = sheet.crop((x, y, x + w, y + h))
            k = scale / res
            pm = img.convert("RGBa")
            if abs(k - 1) > 1e-3:
                nw, nh = max(1, int(round(img.size[0] * k))), max(1, int(round(img.size[1] * k)))
                pm = pm.resize((nw, nh), Image.LANCZOS)
            kx = pm.size[0] / img.size[0] / (1 / res) if img.size[0] else 1
            ky = pm.size[1] / img.size[1] / (1 / res) if img.size[1] else 1
            self._sprite_cache[key] = (pm, kx, ky)
        return self._sprite_cache[key]

    def sprite_image(self, name, scale):
        """-> (premultiplied RGBA image at atlas scale, logical width, logical height)"""
        sheet, x, y, w, h, rot, res = self.sprites[name]
        pm, kx, ky = self._sprite_img(name, scale)
        img = Image.frombytes("RGBA", pm.size, pm.tobytes())
        return img, w / res, h / res

    def render(self, tl, i, scale):
        """Renders frame i -> (premultiplied RGBA image, x0, y0 logical) or (None, ...)."""
        bb, items = self.frame_bounds(tl, i)
        if bb is None:
            return None, 0, 0, 0, 0
        x0, y0 = math.floor(bb[0]), math.floor(bb[1])
        W = max(1, int(math.ceil((bb[2] - x0) * scale)) + 1)
        H = max(1, int(math.ceil((bb[3] - y0) * scale)) + 1)
        canvas = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        for name, m, col in items:
            if name not in self.sprites:
                continue
            pm, kx, ky = self._sprite_img(name, scale)
            # sprite pixel q -> logical u = q / k ; canvas = scale * (M u - (x0, y0))
            a = scale * m[0] / kx
            b = scale * m[1] / kx
            c = scale * m[2] / ky
            d = scale * m[3] / ky
            tx = scale * (m[4] - x0)
            ty = scale * (m[5] - y0)
            det = a * d - b * c
            if abs(det) < 1e-9:
                continue
            # inverse: q = Inv * (p - t)
            ia, ib, ic, id_ = d / det, -c / det, -b / det, a / det
            data = (ia, ib, -(ia * tx + ib * ty), ic, id_, -(ic * tx + id_ * ty))
            src = pm
            if col is not None:
                src = _apply_color(pm, col)
            layer = src.transform((W, H), Image.AFFINE, data, resample=Image.BICUBIC)
            layer = layer.convert("RGBA")
            canvas.alpha_composite(layer)
        return canvas, x0, y0, (bb[2] - x0), (bb[3] - y0)


def _apply_color(pm, col):
    """pm: 'RGBa' premultiplied image. Applies a Flash colour transform."""
    rm, gm, bm, am, ro, go, bo, ao = col
    img = pm.convert("RGBA")
    r, g, b, a = img.split()
    lut = lambda mul, off: [max(0, min(255, int(v * mul + off))) for v in range(256)]
    r = r.point(lut(rm, ro))
    g = g.point(lut(gm, go))
    b = b.point(lut(bm, bo))
    a = a.point(lut(am, ao))
    return Image.merge("RGBA", (r, g, b, a)).convert("RGBa")


def find_label_frames(tl, label):
    for layer in tl.layers:
        found = []
        for fr in layer["frames"]:
            if fr[2] is not None and fr[2].rstrip() == label:
                found.extend(range(fr[0], fr[0] + fr[1]))
        if found:
            return found
    return []


# ---------------------------------------------------------------- packing / writing

class AtlasBuilder:
    """Collects images, frames and animations, packs them and writes <out>.atl + pages.

    An image is a premultiplied RGBA picture with a logical size (game units).
    A frame is its untrimmed size plus pieces: (image, matrix, colour), the
    matrix mapping the image's logical coordinates into the frame. Sparrow
    frames have one piece; Animate frames have one per visible atlas sprite.
    """

    def __init__(self, scale, nearest):
        self.scale = scale
        self.nearest = nearest
        self.images = []        # (img, lw, lh)
        self.by_hash = {}
        self.frames = []        # (fw, fh, [(img_index, matrix, colour_rgba)])
        self.anims = {}
        self.fps = 24

    def add_image(self, img, lw, lh):
        key = hashlib.md5(img.tobytes() + bytes("%s %.3f %.3f" % (img.size, lw, lh), "ascii")).hexdigest()
        if key not in self.by_hash:
            self.by_hash[key] = len(self.images)
            self.images.append((img, lw, lh))
        return self.by_hash[key]

    def add_frame(self, fr):
        """Adds a fnfatlas.Frame (one trimmed image)."""
        pieces = []
        if fr.img is not None:
            ii = self.add_image(fr.img, fr.w, fr.h)
            pieces.append((ii, (1.0, 0.0, 0.0, 1.0, fr.ox, fr.oy), 0xFFFFFFFF))
        return self.add_pieces(fr.fw, fr.fh, pieces)

    def add_pieces(self, fw, fh, pieces):
        self.frames.append((fw, fh, pieces))
        return len(self.frames) - 1

    def write(self, out_base):
        T = PAGE - PAD - 2
        tiles = []          # (img_index, tile_img, tx, ty, iw, ih, ml, mt)
        img_tiles = []
        for k, (im, lw, lh) in enumerate(self.images):
            w, h = im.size
            lst = []
            if w + PAD <= PAGE and h + PAD <= PAGE:
                lst.append(len(tiles))
                tiles.append((k, im, 0, 0, w, h, 0, 0))
            else:
                for ty in range(0, h, T):
                    for tx in range(0, w, T):
                        iw, ih = min(T, w - tx), min(T, h - ty)
                        ml, mt = (1 if tx > 0 else 0), (1 if ty > 0 else 0)
                        mr, mb = (1 if tx + iw < w else 0), (1 if ty + ih < h else 0)
                        tile = im.crop((tx - ml, ty - mt, tx + iw + mr, ty + ih + mb))
                        lst.append(len(tiles))
                        tiles.append((k, tile, tx, ty, iw, ih, ml, mt))
            img_tiles.append(lst)
        order = sorted(range(len(tiles)), key=lambda p: (-tiles[p][1].size[1], -tiles[p][1].size[0]))
        pages = []
        place = {}
        for p in order:
            w, h = tiles[p][1].size
            pos = None
            for pi, pg in enumerate(pages):
                pos = _shelf_place(pg, w + PAD, h + PAD)
                if pos:
                    place[p] = (pi, pos[0], pos[1])
                    break
            if not pos:
                pg = {"shelves": [], "y": 0, "w": 0, "h": 0}
                pages.append(pg)
                pos = _shelf_place(pg, w + PAD, h + PAD)
                place[p] = (len(pages) - 1, pos[0], pos[1])
        page_imgs = []
        for pg in pages:
            pw = max(4, (pg["w"] + 3) & ~3)
            ph = max(4, (pg["h"] + 3) & ~3)
            page_imgs.append(Image.new("RGBA", (min(PAGE, pw), min(PAGE, ph)), (0, 0, 0, 0)))
        for p, (pi, x, y) in place.items():
            page_imgs[pi].paste(tiles[p][1], (x + 1, y + 1))
        names = []
        for pi, im in enumerate(page_imgs):
            name = "%s_%d.tex" % (os.path.basename(out_base), pi)
            write_tex(os.path.join(os.path.dirname(out_base), name), im)
            names.append(name)

        # runtime pieces: page, u, v, uw, vh (texture px), matrix texture px -> frame, colour
        rpieces = []
        rframes = []
        for fw, fh, pieces in self.frames:
            first = len(rpieces)
            for ii, m, col in pieces:
                im, lw, lh = self.images[ii]
                W, H = im.size
                sx, sy = lw / W, lh / H
                for p in img_tiles[ii]:
                    _, tile, tx, ty, iw, ih, ml, mt = tiles[p]
                    pi, x, y = place[p]
                    # tile px (q) -> image logical: (tx + q) * s ; then m
                    a, b, c, d, e, f = m
                    pm = (a * sx, b * sx, c * sy, d * sy,
                          a * tx * sx + c * ty * sy + e, b * tx * sx + d * ty * sy + f)
                    rpieces.append((pi, x + 1 + ml, y + 1 + mt, iw, ih, pm, col))
            rframes.append((fw, fh, first, len(rpieces) - first))
        os.makedirs(os.path.dirname(out_base), exist_ok=True)
        with open(out_base + ".atl", "wb") as f:
            anims = [(k, v) for k, v in self.anims.items()]
            f.write(b"FAT2")
            f.write(struct.pack(">HHIHBB", len(names), len(rframes), len(rpieces), len(anims),
                                1 if self.nearest else 0, int(self.fps) & 0xFF))
            for n in names:
                nb = n.encode()[:63]
                f.write(nb + b"\0" * (64 - len(nb)))
            for pi, u, v, uw, vh, m, col in rpieces:
                f.write(struct.pack(">BxHHHH6fI", pi, u, v, uw, vh, *m, col & 0xFFFFFFFF))
            for fw, fh, first, cnt in rframes:
                f.write(struct.pack(">ffIH2x", fw, fh, first, cnt))
            for k, v in anims:
                kb = k.encode()
                f.write(struct.pack(">H", len(kb)) + kb)
                f.write(struct.pack(">H", len(v)))
                f.write(struct.pack(">%dH" % len(v), *v))
        return sum(im.size[0] * im.size[1] * 2 for im in page_imgs)


def r2(v):
    return round(float(v), 2)


def _pow2(v):
    p = 4
    while p < v:
        p *= 2
    return p


def _shelf_place(pg, w, h):
    for sh in pg["shelves"]:
        if h <= sh[1] and sh[2] + w <= PAGE:
            x = sh[2]
            sh[2] += w
            pg["w"] = max(pg["w"], sh[2])
            return x, sh[0]
    if pg["y"] + h <= PAGE and w <= PAGE:
        y = pg["y"]
        pg["shelves"].append([y, h, w])
        pg["y"] += h
        pg["w"] = max(pg["w"], w)
        pg["h"] = pg["y"]
        return 0, y
    return None


# ---------------------------------------------------------------- animation lookup (Flixel rules)

def frames_by_prefix(frames, prefix):
    found = [i for i, f in enumerate(frames) if f.name.startswith(prefix)]
    if not found:
        return []
    name = frames[found[0]].name
    post = name.find(".", len(prefix))
    postfix = "" if post == -1 else name[post:]

    def num(i):
        s = frames[i].name[len(prefix):len(frames[i].name) - len(postfix)]
        m = re.match(r"\s*(-?\d+)", s)
        return int(m.group(1)) if m else 0

    return sorted(found, key=num)


def frames_by_indices(frames, prefix, indices, postfix=""):
    out = []
    for idx in indices:
        for i, f in enumerate(frames):
            n = f.name
            if n.startswith(prefix) and n.endswith(postfix):
                s = n[len(prefix):len(n) - len(postfix)] if postfix else n[len(prefix):]
                try:
                    if int(s) == idx:
                        out.append(i)
                        break
                except ValueError:
                    pass
    return out
