#!/usr/bin/env python3
"""Converts the Friday Night Funkin' assets for FunkinWii.

The game's art and music are not redistributable, so FunkinWii ships
without them; this script builds its data folder from your own copy of the
assets (https://github.com/FunkinCrew/Funkin.assets, or the assets/ folder
of a desktop build).

    python3 tools/convert.py path/to/Funkin.assets out/

Then copy out/ to the SD card as sd:/apps/FunkinWii/data/ (the folder
next to boot.dol).

Needs Python 3.8+ and Pillow (pip install pillow).
"""

import argparse
import glob
import json
import os
import re
import shutil
import sys
import time
from concurrent.futures import ProcessPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fnfatlas as fa  # noqa: E402
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

LIBS = ["preload", "shared", "tutorial", "week1", "week2", "week3", "week4", "week5", "week6", "week7", "weekend1", "sserafim"]

A = None  # asset root
OUT = None
SCALE = 0.5


# ------------------------------------------------------------------ helpers

def jload(path):
    with open(path, encoding="utf-8-sig") as f:
        return json.load(f)


def jsave(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(data, f, separators=(",", ":"))


def split_lib(path):
    if ":" in path:
        lib, rest = path.split(":", 1)
        if lib == "default":
            lib = None
        return lib, rest
    return None, path


def find_graphic(path, hint=()):
    """Resolves a game image path to (kind, lib, rel, absbase) like Paths.image()."""
    lib, rel = split_lib(path)
    order = []
    if lib:
        order.append(lib)
    order += [h for h in hint if h]
    order += ["shared", "preload"] + LIBS
    seen = set()
    for l in order:
        if l in seen:
            continue
        seen.add(l)
        base = os.path.join(A, l, "images", rel)
        if os.path.isfile(os.path.join(base, "Animation.json")):
            return ("animate", l, rel, base)
        if os.path.isfile(base + ".png"):
            if os.path.isfile(base + ".xml"):
                return ("sparrow", l, rel, base)
            if os.path.isfile(base + ".txt"):
                return ("packer", l, rel, base)
            return ("image", l, rel, base)
    return None


class Registry:
    """Every graphic the game will load, with the animations wanted from it."""

    def __init__(self):
        self.items = {}

    def use(self, path, hint=(), anims=(), pixel=False, all_prefixes=False, icon=False):
        g = find_graphic(path, hint)
        if g is None:
            print("  missing image: %s" % path)
            return None
        kind, lib, rel, base = g
        out = "img/%s/%s" % (lib, rel)
        it = self.items.setdefault(out, {"kind": kind, "base": base, "anims": {}, "pixel": False,
                                         "all": False, "icon": False})
        it["pixel"] = it["pixel"] or pixel
        it["all"] = it["all"] or all_prefixes
        it["icon"] = it["icon"] or icon
        keys = []
        for a in anims:
            k = anim_key(a)
            it["anims"][k] = a
            keys.append(k)
        return out, keys


def anim_key(a):
    t = a.get("animType") or "framelabel"
    idx = a.get("frameIndices") or a.get("indices") or []
    return "%s|%s|%s" % (t, a.get("prefix", ""), ",".join(str(i) for i in idx))


def compile_item(args):
    out, it, scale = args
    try:
        return _compile_item(out, it, scale)
    except Exception as e:  # keep going; report at the end
        import traceback
        return (out, 0, "ERROR %s\n%s" % (e, traceback.format_exc()))


def _compile_item(out, it, scale):
    pixel = it["pixel"]
    sc = 1.0 if pixel else scale
    kind, base = it["kind"], it["base"]
    ab = fa.AtlasBuilder(sc, pixel)
    if kind in ("sparrow", "packer", "image"):
        if kind == "sparrow":
            frames = fa.load_sparrow(base + ".png", base + ".xml", sc, pixel)
        elif kind == "packer":
            frames = fa.load_packer(base + ".png", base + ".txt", sc, pixel)
        else:
            frames = fa.load_image(base + ".png", sc, pixel)
            if it["icon"]:
                frames = split_icon(base + ".png", sc, pixel)
        for f in frames:
            ab.add_frame(f)
        anims = dict(it["anims"])
        if it["all"]:
            for f in frames:
                p = f.name[:-4] if re.search(r"\d{4}$", f.name) else re.sub(r"\d+$", "", f.name)
                anims.setdefault("sparrow|%s|" % p, {"prefix": p})
        for k, a in anims.items():
            idx = a.get("frameIndices") or a.get("indices")
            if idx and kind == "packer":
                lst = [i for i in idx if 0 <= i < len(frames)]
            elif idx:
                lst = fa.frames_by_indices(frames, a.get("prefix", ""), idx)
            else:
                lst = fa.frames_by_prefix(frames, a.get("prefix", ""))
            if lst:
                ab.anims[k] = lst
    else:
        at = fa.AnimateAtlas(base)
        cache = {}
        sprite_ids = {}
        reqs = dict(it["anims"])
        if not reqs:
            reqs["framelabel||"] = {"prefix": ""}
        for k, a in reqs.items():
            t = a.get("animType") or "framelabel"
            prefix = a.get("prefix", "")
            tl = None
            lst = []
            if t == "symbol":
                tl = at.symbols.get(prefix)
                if tl:
                    lst = list(range(tl.frame_count))
            elif prefix == "":
                tl = at.main
                lst = list(range(tl.frame_count))
            else:
                tl = at.main
                lst = fa.find_label_frames(tl, prefix)
                if not lst and prefix in at.symbols:
                    tl = at.symbols[prefix]
                    lst = list(range(tl.frame_count))
            if not lst or tl is None:
                continue
            idx = a.get("frameIndices") or a.get("indices")
            if idx:
                lst = [lst[i] for i in idx if 0 <= i < len(lst)]
            bounds = at.timeline_bounds(tl)
            fw, fh = bounds[2] - bounds[0], bounds[3] - bounds[1]
            ids = []
            for i in lst:
                ck = (tl.name, i)
                if ck not in cache:
                    items = []
                    at.collect(tl, i, (1, 0, 0, 1, -bounds[0], -bounds[1]), None, items)
                    pieces = []
                    for name, m, col in items:
                        if name not in at.sprites:
                            continue
                        if name not in sprite_ids:
                            img, lw, lh = at.sprite_image(name, sc)
                            sprite_ids[name] = ab.add_image(img, lw, lh)
                        pieces.append((sprite_ids[name], m, color_word(col)))
                    cache[ck] = ab.add_pieces(fw, fh, pieces)
                ids.append(cache[ck])
            ab.anims[k] = ids
        ab.fps = at.framerate or 24
    size = ab.write(os.path.join(OUT, out))
    return (out, size, None)


def color_word(col):
    if col is None:
        return 0xFFFFFFFF
    rm, gm, bm, am, ro, go, bo, ao = col
    ch = lambda m, o: max(0, min(255, int(round((m + o / 255.0) * 255))))
    return (ch(rm, ro) << 24) | (ch(gm, go) << 16) | (ch(bm, bo) << 8) | ch(am, ao)


def split_icon(png, sc, pixel):
    """Health icons: a row of square frames (normal, losing, ...)."""
    img = Image.open(png).convert("RGBA")
    w, h = img.size
    n = max(1, w // h) if w >= h * 2 else 1
    fw = w // n
    frames = []
    for i in range(n):
        sub = img.crop((i * fw, 0, (i + 1) * fw, h))
        frames.append(fa.Frame("icon%d" % i, sub, 0, 0, fw, h, fw, h))
    return fa.finish_frames(frames, sc, pixel, trim=True)


# ------------------------------------------------------------------ data

def lib_of_dir(d):
    return d if d in LIBS else None


def convert_characters(reg):
    out = {}
    for p in sorted(glob.glob(os.path.join(A, "preload/data/characters/*.json"))):
        cid = os.path.splitext(os.path.basename(p))[0]
        d = jload(p)
        pixel = bool(d.get("isPixel", False))
        base_path = d["assetPath"]
        anims = []
        for a in d.get("animations", []):
            if not a.get("prefix") and a.get("prefix") != "":
                continue
            ap = a.get("assetPath") or base_path
            spec = {"prefix": a.get("prefix", ""), "animType": a.get("animType") or ("framelabel" if d.get("renderType", "sparrow").endswith("animateatlas") else "sparrow"),
                    "frameIndices": a.get("frameIndices")}
            r = reg.use(ap, anims=[spec], pixel=pixel)
            if r is None:
                continue
            anims.append({
                "name": a["name"],
                "atlas": r[0],
                "key": r[1][0],
                "fps": a.get("frameRate", 24),
                "loop": bool(a.get("looped", False)),
                "offsets": a.get("offsets") or [0, 0],
                "flipX": bool(a.get("flipX", False)),
            })
        hi = d.get("healthIcon") or {}
        icon_id = hi.get("id") or cid
        icon_pixel = hi.get("isPixel", pixel)
        icon = None
        r = reg.use("icons/icon-%s" % icon_id, pixel=bool(icon_pixel), icon=True)
        if r is None:
            r = reg.use("icons/icon-face", pixel=False, icon=True)
        if r:
            icon = r[0]
        death = d.get("death") or {}
        c = {
            "id": cid,
            "name": d.get("name", cid),
            "scale": d.get("scale", 1.0) if d.get("scale") is not None else 1.0,
            "isPixel": pixel,
            "flipX": bool(d.get("flipX", False)),
            "offsets": d.get("offsets") or [0, 0],
            "cameraOffsets": d.get("cameraOffsets") or [0, 0],
            "singTime": d.get("singTime", 8.0) or 8.0,
            "danceEvery": d.get("danceEvery", 1.0) if d.get("danceEvery") is not None else 1.0,
            "startingAnimation": d.get("startingAnimation") or "idle",
            "icon": icon,
            "iconScale": hi.get("scale", 1.0) or 1.0,
            "iconFlipX": bool(hi.get("flipX", False)),
            "iconPixel": bool(icon_pixel),
            "iconOffsets": hi.get("offsets") or [0, 0],
            "iconBop": hi.get("shouldBop", True) if hi.get("shouldBop") is not None else True,
            "deathCameraOffsets": death.get("cameraOffsets") or [0, 0],
            "deathCameraZoom": death.get("cameraZoom", 1.0) or 1.0,
            "anims": anims,
        }
        jsave(os.path.join(OUT, "chars/%s.json" % cid), c)
        out[cid] = c
    print("characters: %d" % len(out))
    return out


def convert_stages(reg):
    n = 0
    for p in sorted(glob.glob(os.path.join(A, "preload/data/stages/*.json"))):
        sid = os.path.splitext(os.path.basename(p))[0]
        d = jload(p)
        hint = [lib_of_dir(d.get("directory", ""))]
        props = []
        for pr in d.get("props", []):
            ap = pr.get("assetPath", "")
            q = {
                "name": pr.get("name", ""),
                "position": pr.get("position", [0, 0]),
                "zIndex": pr.get("zIndex", 0),
                "scroll": pr.get("scroll", [1, 1]),
                "scale": pr.get("scale", [1, 1]) if pr.get("scale") is not None else [1, 1],
                "alpha": pr.get("alpha", 1.0),
                "angle": pr.get("angle", 0),
                "flipX": bool(pr.get("flipX", False)),
                "danceEvery": pr.get("danceEvery", 0) or 0,
                "startingAnimation": pr.get("startingAnimation"),
                "isPixel": bool(pr.get("isPixel", False)),
                "color": pr.get("color"),
                "blend": pr.get("blend"),
                "flipY": bool(pr.get("flipY", False)),
                "anims": [],
            }
            if isinstance(q["scale"], (int, float)):
                q["scale"] = [q["scale"], q["scale"]]
            if ap.startswith("#"):
                q["solid"] = ap
                props.append(q)
                continue
            at = pr.get("animType") or "sparrow"
            specs = []
            for a in pr.get("animations", []):
                specs.append({"prefix": a.get("prefix", ""), "frameIndices": a.get("frameIndices"),
                              "animType": a.get("animType") or ("framelabel" if at == "animateatlas" else "sparrow")})
            r = reg.use(ap, hint=hint, anims=specs, pixel=q["isPixel"])
            if r is None:
                continue
            q["atlas"] = r[0]
            for a, k in zip(pr.get("animations", []), r[1]):
                q["anims"].append({"name": a["name"], "key": k, "fps": a.get("frameRate", 24),
                                   "loop": bool(a.get("looped", False)), "offsets": a.get("offsets") or [0, 0],
                                   "flipX": bool(a.get("flipX", False))})
            props.append(q)
        chars = {}
        for k, c in d.get("characters", {}).items():
            chars[k] = {"zIndex": c.get("zIndex", 0), "position": c.get("position", [0, 0]),
                        "cameraOffsets": c.get("cameraOffsets", [0, 0]), "scroll": c.get("scroll", [1, 1]),
                        "scale": c.get("scale", 1.0) if c.get("scale") is not None else 1.0}
        jsave(os.path.join(OUT, "stages/%s.json" % sid), {"name": d.get("name", sid), "cameraZoom": d.get("cameraZoom", 1.0),
                                                          "props": props, "characters": chars})
        n += 1
    print("stages: %d" % n)


def convert_levels(reg):
    order = ["tutorial", "week1", "week2", "week3", "week4", "week5", "week6", "week7", "weekend1"]
    found = [os.path.splitext(os.path.basename(p))[0] for p in glob.glob(os.path.join(A, "preload/data/levels/*.json"))]
    for f in sorted(found):
        if f not in order:
            order.append(f)
    weeks = []
    for wid in order:
        p = os.path.join(A, "preload/data/levels/%s.json" % wid)
        if not os.path.exists(p):
            continue
        d = jload(p)
        if d.get("visible") is False:
            continue
        title = reg.use(d.get("titleAsset", "storymenu/titles/%s" % wid))
        props = []
        for pr in d.get("props", []):
            specs = [{"prefix": a.get("prefix", ""), "frameIndices": a.get("frameIndices"), "animType": "sparrow"} for a in pr.get("animations", [])]
            r = reg.use(pr["assetPath"], anims=specs, pixel=bool(pr.get("isPixel", False)))
            if r is None:
                continue
            props.append({"atlas": r[0], "scale": pr.get("scale", 1.0), "offsets": pr.get("offsets", [0, 0]),
                          "danceEvery": pr.get("danceEvery", 1.0),
                          "anims": [{"name": a["name"], "key": k, "fps": a.get("frameRate", 24), "loop": bool(a.get("looped", False)),
                                     "offsets": a.get("offsets") or [0, 0]} for a, k in zip(pr.get("animations", []), r[1])]})
        weeks.append({"id": wid, "name": d.get("name", wid), "title": title[0] if title else None, "props": props,
                      "background": d.get("background", "#F9CF51"), "songs": d.get("songs", []),
                      "difficulties": d.get("difficulties", ["easy", "normal", "hard"])})
    jsave(os.path.join(OUT, "weeks.json"), weeks)
    print("weeks: %d" % len(weeks))
    return weeks


def convert_notestyles(reg):
    for p in glob.glob(os.path.join(A, "preload/data/notestyles/*.json")):
        nid = os.path.splitext(os.path.basename(p))[0]
        d = jload(p)
        out = {"id": nid}
        assets = d.get("assets", {})
        for k, v in assets.items():
            if not isinstance(v, dict):
                continue
            pixel = bool(v.get("isPixel", False))
            entry = {"scale": v.get("scale", 1.0), "offsets": v.get("offsets", [0, 0]), "isPixel": pixel,
                     "alpha": v.get("alpha", 1.0)}
            data = v.get("data", {}) or {}
            ap = v.get("assetPath")
            if k == "holdNoteCover":
                entry["dirs"] = {}
                for dname in ("left", "down", "up", "right"):
                    dd = data.get(dname)
                    if not dd:
                        continue
                    specs = [dict(dd[s], animType="sparrow") for s in ("start", "hold", "end") if s in dd]
                    cap = dd.get("assetPath") or ap
                    if not cap:
                        continue
                    r = reg.use(cap, anims=specs, pixel=pixel)
                    if r:
                        entry["dirs"][dname] = {"atlas": r[0], "keys": r[1]}
            elif k == "noteSplash":
                entry["dirs"] = {}
                for dname in ("left", "down", "up", "right"):
                    lst = data.get(dname + "Splashes") or []
                    specs = [dict(s, animType="sparrow") for s in lst]
                    r = reg.use(ap, anims=specs, pixel=pixel)
                    if r:
                        entry["atlas"] = r[0]
                        entry["dirs"][dname] = r[1]
            elif ap:
                specs = []
                names = []
                for n, s in data.items():
                    if isinstance(s, dict) and "prefix" in s:
                        specs.append({"prefix": s["prefix"], "frameIndices": s.get("frameIndices"), "animType": "sparrow"})
                        names.append(n)
                r = reg.use(ap, anims=specs, pixel=pixel)
                if r:
                    entry["atlas"] = r[0]
                    entry["anims"] = dict(zip(names, r[1]))
            if isinstance(data, dict) and data.get("audioPath"):
                entry["sound"] = sound_path(data["audioPath"])
            out[k] = entry
        jsave(os.path.join(OUT, "notestyles/%s.json" % nid), out)
    print("note styles done")


def sound_path(p):
    lib, rel = split_lib(p)
    for l in ([lib] if lib else []) + ["shared", "preload"] + LIBS:
        for sub in ("sounds", "music"):
            f = os.path.join(A, l, sub, rel + ".ogg")
            if os.path.exists(f):
                return "snd/%s/%s/%s.ogg" % (l, sub, rel)
    return None


def convert_ui(reg):
    ui = {}

    def u(key, path, anims=(), pixel=False, all_prefixes=False):
        r = reg.use(path, anims=[dict(a, animType="sparrow") for a in anims], pixel=pixel, all_prefixes=all_prefixes)
        if r:
            ui[key] = {"atlas": r[0], "keys": r[1]}

    u("alphabet", "preload:alphabet", all_prefixes=True)
    u("fontBold", "preload:fonts/bold", all_prefixes=True)
    u("fontDefault", "preload:fonts/default", all_prefixes=True)
    u("logo", "preload:logoBumpin", [{"prefix": "logo bumpin"}])
    u("gfTitle", "preload:gfDanceTitle", [{"prefix": "gfDance", "frameIndices": [30, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14]},
                                           {"prefix": "gfDance", "frameIndices": list(range(15, 30))}])
    u("titleEnter", "preload:title-screen-text", [{"prefix": "Idle"}, {"prefix": "Confirm"}])
    u("menuBG", "preload:menuBG")
    u("menuBGMagenta", "preload:menuBGMagenta")
    u("menuDesat", "preload:menuDesat")
    for item in ("storymode", "freeplay", "options", "credits"):
        u("menu_" + item, "preload:mainmenu/" + item, all_prefixes=True)
    for diff in ("easy", "normal", "hard", "erect", "nightmare"):
        u("diff_" + diff, "preload:storymenu/difficulties/" + diff, all_prefixes=True)
    u("storyArrows", "preload:storymenu/ui/arrows", all_prefixes=True)
    u("healthBar", "shared:healthBar")
    for c in ("ready", "set", "go"):
        u("countdown_" + c, "shared:ui/countdown/funkin/" + c)
        u("countdownPixel_" + c, "shared:ui/countdown/pixel/" + c, pixel=True)
    for j in ("sick", "good", "bad", "shit", "combo") + tuple("num%d" % i for i in range(10)):
        u("popup_" + j, "preload:ui/popup/funkin/" + j)
        u("popupPixel_" + j, "preload:ui/popup/pixel/" + j, pixel=True)
    u("pauseCircle", "preload:pauseCircle")
    jsave(os.path.join(OUT, "ui.json"), ui)


def copy_audio():
    n = 0
    for lib in LIBS:
        for sub in ("sounds", "music"):
            root = os.path.join(A, lib, sub)
            for p in glob.glob(os.path.join(root, "**/*.ogg"), recursive=True):
                rel = os.path.relpath(p, root)
                dst = os.path.join(OUT, "snd", lib, sub, rel)
                if not os.path.exists(dst) or os.path.getsize(dst) != os.path.getsize(p):
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    shutil.copyfile(p, dst)
                n += 1
    print("sounds and music: %d files" % n)


def convert_songs(chars):
    songs = []
    for d in sorted(glob.glob(os.path.join(A, "preload/data/songs/*"))):
        sid = os.path.basename(d)
        if not os.path.isfile(os.path.join(d, "%s-metadata.json" % sid)):
            continue
        dst = os.path.join(OUT, "songs", sid)
        os.makedirs(dst, exist_ok=True)
        for f in glob.glob(os.path.join(d, "*.json")):
            shutil.copyfile(f, os.path.join(dst, os.path.basename(f)))
        for f in glob.glob(os.path.join(A, "songs", sid, "*.ogg")):
            t = os.path.join(dst, os.path.basename(f))
            if not os.path.exists(t) or os.path.getsize(t) != os.path.getsize(f):
                shutil.copyfile(f, t)
        meta = jload(os.path.join(d, "%s-metadata.json" % sid))
        songs.append({"id": sid, "name": meta.get("songName", sid), "variations": meta.get("playData", {}).get("songVariations", [])})
    jsave(os.path.join(OUT, "songs.json"), songs)
    print("songs: %d" % len(songs))


def make_font(name, ttf, size):
    font = ImageFont.truetype(ttf, size)
    glyphs = {}
    frames = []
    asc, desc = font.getmetrics()
    for code in range(32, 127):
        ch = chr(code)
        bbox = font.getbbox(ch)
        adv = font.getlength(ch)
        w = max(1, bbox[2] - bbox[0])
        h = max(1, bbox[3] - bbox[1])
        img = Image.new("RGBA", (w + 2, h + 2), (255, 255, 255, 0))
        dr = ImageDraw.Draw(img)
        dr.text((1 - bbox[0], 1 - bbox[1]), ch, font=font, fill=(255, 255, 255, 255))
        frames.append(fa.Frame(ch, img, bbox[0] - 1, bbox[1] - 1, w + 2, h + 2, adv, asc + desc))
        glyphs[str(code)] = [len(frames) - 1, round(adv, 2)]
    frames = fa.finish_frames(frames, 1.0, True, trim=False)
    ab = fa.AtlasBuilder(1.0, False)
    for f in frames:
        ab.add_frame(f)
    ab.anims["__glyphs"] = []
    ab.write(os.path.join(OUT, "font", name))
    jsave(os.path.join(OUT, "font", name + ".json"), {"size": size, "line": asc + desc, "ascent": asc, "glyphs": glyphs})


def main():
    global A, OUT, SCALE
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("assets", help="Funkin.assets checkout (has preload/, shared/, songs/ ...)")
    ap.add_argument("out", help="output folder (copy to sd:/apps/FunkinWii/data)")
    ap.add_argument("--scale", type=float, default=0.5, help="texture scale (default 0.5)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--only", help="only build graphics whose output path contains this text")
    ap.add_argument("--no-audio", action="store_true", help="skip copying songs and sounds")
    args = ap.parse_args()
    A = os.path.abspath(args.assets)
    OUT = os.path.abspath(args.out)
    SCALE = args.scale
    if not os.path.isdir(os.path.join(A, "preload", "data")):
        sys.exit("%s does not look like the Funkin assets folder (no preload/data)" % A)
    os.makedirs(OUT, exist_ok=True)
    t0 = time.time()
    reg = Registry()
    chars = convert_characters(reg)
    convert_stages(reg)
    convert_levels(reg)
    convert_notestyles(reg)
    convert_ui(reg)
    font_dir = os.path.join(A, "fonts")
    make_font("vcr", os.path.join(font_dir, "vcr.ttf"), 32)
    if not args.no_audio:
        convert_songs(chars)
        copy_audio()
    items = [(k, v, SCALE) for k, v in sorted(reg.items.items()) if not args.only or args.only in k]
    print("graphics: %d" % len(items))
    total = 0
    errors = []
    with ProcessPoolExecutor(max_workers=args.jobs, initializer=_init_worker, initargs=(A, OUT)) as ex:
        for out, size, err in ex.map(compile_item, items, chunksize=1):
            total += size
            if err:
                errors.append((out, err))
                print("  %s: %s" % (out, err.splitlines()[0]))
            else:
                print("  %-60s %7.1f KB" % (out, size / 1024))
    print("texture data: %.1f MB, %d errors, %.0f s" % (total / 1048576, len(errors), time.time() - t0))
    with open(os.path.join(OUT, "version.txt"), "w") as f:
        f.write("1\n")
    for out, err in errors:
        print(out, err)


def _init_worker(a, out):
    global A, OUT
    A, OUT = a, out


if __name__ == "__main__":
    main()
