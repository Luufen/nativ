"""A game's data mods, laid as the Vita plugin rePatch lays them (REPATCH-01): loose files over the game's app0.

On the console a rePatch mod is the folder ux0:rePatch/<title>/: a file there is read in place of app0's file of the
same path (a translation patch replaces the game's data archives that way, its program stays the game's). Here each mod
the player adds lies in the library as dump/decrypted/<title>/repatch/<mod>/ -- the contents of that folder -- and every
run's fs/repatch links there (vitarecomp.link_mods), so the runtime reads it at start (runtime/core/repatch.h). A mod
is off while repatch/<mod>.off is there; nothing is built for a mod, and none of its files reaches the build.

Accepted: a folder or a .zip holding either the files themselves, or rePatch/<title>/..., or <title>/... (as mods are
passed around). A mod made for another title id (another region's release) is refused: its files probably don't fit
this copy's data (--any-title takes it anyway). A mod never changes the game's program: Nativ's game is recompiled from
the eboot.bin its pack recognises by SHA-256, so eboot.bin, sce_module/ and .self/.suprx/.elf files are left out, each
named.

  mods.py add <library title folder> <title> <folder|zip> [--name N] [--any-title]
  mods.py list <library title folder>          one line each: name, on|off, files
  mods.py set <library title folder> <name> on|off
  mods.py remove <library title folder> <name>
  mods.py selftest
"""
import os
import re
import shutil
import stat
import sys
import tempfile
import zipfile

TITLE_RE = re.compile(r"^[A-Z]{4}[0-9]{5}$")
NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9 _.()+-]{0,63}$")


class ModError(Exception):
    pass


def blocked(rel: str) -> bool:
    """a path in app0 a mod may not replace (runtime/core/repatch.h repatch_blocked, the same rule)"""
    r = rel.replace("\\", "/").lstrip("/")
    low = r.lower()
    if low == "sce_module" or low.startswith("sce_module/"): return True
    base = low.rsplit("/", 1)[-1]
    return base.startswith("eboot.bin") or os.path.splitext(base)[1] in (".self", ".suprx", ".sprx", ".elf")


def name_ok(n: str) -> bool:
    return bool(NAME_RE.match(n)) and not n.lower().endswith(".off") and n not in (".", "..")


def _dirs(d):
    try: return [e for e in os.listdir(d) if os.path.isdir(os.path.join(d, e))]
    except OSError: return []


def find_root(src: str, title: str, any_title: bool = False, wrapper: str = "") -> tuple[str, str]:
    """the folder in src whose contents lie over app0, and the title id the mod names ("" none). A mod made for
    another title is a ModError unless any_title. wrapper: a zip's name -- its one top folder of that name is only
    the zip's wrapping (a lone top folder of another name, such as data/, is the mod's own)."""
    root, named = src, ""
    for i in range(3):   # (a zip's one wrapping folder, then rePatch/, then <title>/)
        entries = [e for e in os.listdir(root) if not e.startswith(".") and e != "__MACOSX"]
        sub = _dirs(root)
        rp = [e for e in sub if e.lower() == "repatch"]
        ids = [e for e in sub if TITLE_RE.match(e.upper())]
        if rp: root = os.path.join(root, rp[0]); continue
        if ids and len(entries) == len(ids):   # only title folders here: the mod's own
            mine = [e for e in ids if e.upper() == title]
            if mine: return os.path.join(root, mine[0]), title
            if any_title and len(ids) == 1: return os.path.join(root, ids[0]), ids[0].upper()
            raise ModError(f"this mod is made for {', '.join(sorted(e.upper() for e in ids))}, not {title}: another release's "
                           f"files probably don't fit this copy's data (--any-title takes it anyway)")
        if i == 0 and wrapper and len(entries) == 1 and sub == entries and sub[0].lower() == wrapper.lower():
            root = os.path.join(root, sub[0]); continue   # (the zip's wrapping folder)
        break
    return root, named


def _extract(zpath: str, dst: str) -> None:
    with zipfile.ZipFile(zpath) as z:
        for m in z.infolist():
            n = m.filename.replace("\\", "/")
            parts = [p for p in n.split("/") if p not in ("", ".")]
            if n.startswith("/") or ".." in parts or (parts and ":" in parts[0]):
                raise ModError(f"{os.path.basename(zpath)}: a file outside the zip's folder ({m.filename}): refused")
            if not parts: continue
            out = os.path.join(dst, *parts)
            if m.is_dir(): os.makedirs(out, exist_ok=True); continue
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with z.open(m) as f, open(out, "wb") as g: shutil.copyfileobj(f, g)


def _remove_tree(p):
    def onerr(fn, path, _e):
        try: os.chmod(path, stat.S_IWRITE); fn(path)
        except OSError: pass
    if os.path.lexists(p):
        if os.path.isdir(p) and not os.path.islink(p): shutil.rmtree(p, onerror=onerr)
        else: os.remove(p)


def add(W: str, title: str, src: str, name: str = "", any_title: bool = False, say=print) -> tuple[str, int, list]:
    """src (a folder or a .zip) -> W/repatch/<name>. Returns (name, files, refused paths)."""
    if not TITLE_RE.match(title or ""): raise ModError(f"not a title id: {title}")
    src = os.path.abspath(src)
    if not os.path.exists(src): raise ModError(f"{src}: not there")
    mods = os.path.join(W, "repatch"); os.makedirs(mods, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix=".mod.", dir=mods)
    try:
        if os.path.isfile(src):
            if not zipfile.is_zipfile(src): raise ModError(f"{os.path.basename(src)}: neither a folder nor a .zip")
            unz = os.path.join(tmp, "zip"); _extract(src, unz); top, wrap = unz, os.path.splitext(os.path.basename(src))[0]
        else: top, wrap = src, ""
        root, _named = find_root(top, title, any_title, wrap)
        base = name or os.path.splitext(os.path.basename(src.rstrip("/\\")))[0]
        base = re.sub(r"[^A-Za-z0-9 _.()+-]", "_", base).strip(" ._")[:64] or "mod"
        if base.lower() == "repatch" or TITLE_RE.match(base.upper()): base = "mod"
        if not name_ok(base): raise ModError(f"not a name for a mod: {base}")
        out = os.path.join(tmp, "files"); refused = []; n = 0
        for dp, dns, fns in os.walk(root):
            rel_dir = os.path.relpath(dp, root)
            for fn in fns:
                rel = fn if rel_dir == "." else os.path.join(rel_dir, fn)
                if blocked(rel): refused.append(rel.replace("\\", "/")); continue
                d = os.path.join(out, rel); os.makedirs(os.path.dirname(d), exist_ok=True)
                shutil.copyfile(os.path.join(dp, fn), d); n += 1
        for r in refused: say(f"{title}: mod {base}: {r} left out -- a mod can't change the game's program (it is recompiled from the eboot.bin its pack knows)")
        if not n: raise ModError("no files in it a mod can replace" + (" (only the game's program and its modules)" if refused else ""))
        final, k = base, 2
        while os.path.lexists(os.path.join(mods, final)): final = f"{base} ({k})"; k += 1
        os.rename(out, os.path.join(mods, final))
        return final, n, refused
    finally:
        _remove_tree(tmp)


def rows(W: str) -> list:
    """the title's mods: (name, on, files), by name (bytewise, the runtime's order: the first that has a file wins)"""
    d = os.path.join(W, "repatch"); out = []
    for n in sorted(_dirs(d)):
        if not name_ok(n): continue
        files = sum(len(f) for _dp, _dn, f in os.walk(os.path.join(d, n)))
        out.append((n, not os.path.lexists(os.path.join(d, n + ".off")), files))
    return out


def set_on(W: str, name: str, on: bool) -> None:
    d = os.path.join(W, "repatch")
    if not name_ok(name) or not os.path.isdir(os.path.join(d, name)): raise ModError(f"no mod {name}")
    off = os.path.join(d, name + ".off")
    if on and os.path.lexists(off): os.remove(off)
    if not on and not os.path.lexists(off): open(off, "w").close()


def remove(W: str, name: str) -> None:
    d = os.path.join(W, "repatch")
    if not name_ok(name) or not os.path.isdir(os.path.join(d, name)): raise ModError(f"no mod {name}")
    _remove_tree(os.path.join(d, name)); _remove_tree(os.path.join(d, name + ".off"))


def selftest() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        def put(rel, data=b"x"):
            p = os.path.join(tmp, rel); os.makedirs(os.path.dirname(p), exist_ok=True); open(p, "wb").write(data)
        W = os.path.join(tmp, "lib", "PCSH00143")
        # a folder as rePatch wants it: rePatch/<title>/..., its program left out
        put("m1/rePatch/PCSH00143/data/a.cpk", b"MOD"); put("m1/rePatch/PCSH00143/eboot.bin", b"E"); put("m1/rePatch/PCSH00143/sce_module/libfoo.suprx", b"S")
        name, n, ref = add(W, "PCSH00143", os.path.join(tmp, "m1"), say=lambda s: None)
        assert (name, n, sorted(ref)) == ("m1", 1, ["eboot.bin", "sce_module/libfoo.suprx"]), (name, n, ref)
        assert open(os.path.join(W, "repatch", "m1", "data", "a.cpk"), "rb").read() == b"MOD"
        assert not os.path.exists(os.path.join(W, "repatch", "m1", "eboot.bin"))
        # a zip with one wrapping folder and the files themselves; a second with the same name gets its own
        put("z/English Patch/data/b.cpk", b"Z")
        zp = os.path.join(tmp, "English Patch.zip")
        with zipfile.ZipFile(zp, "w") as z: z.write(os.path.join(tmp, "z", "English Patch", "data", "b.cpk"), "English Patch/data/b.cpk")
        assert add(W, "PCSH00143", zp, say=lambda s: None)[:2] == ("English Patch", 1)
        assert add(W, "PCSH00143", zp, say=lambda s: None)[0] == "English Patch (2)"
        assert os.path.isfile(os.path.join(W, "repatch", "English Patch", "data", "b.cpk"))   # (its data/ kept: only the wrapper goes)
        zd = os.path.join(tmp, "only-data.zip")   # a zip of just data/...: data/ is the mod's own folder
        with zipfile.ZipFile(zd, "w") as z: z.writestr("data/d.cpk", b"D")
        assert add(W, "PCSH00143", zd, say=lambda s: None)[:2] == ("only-data", 1)
        assert os.path.isfile(os.path.join(W, "repatch", "only-data", "data", "d.cpk")); remove(W, "only-data")
        # made for another release (the Japanese PCSG00351): refused, unless asked for
        put("jp/rePatch/PCSG00351/data/c.cpk")
        try: add(W, "PCSH00143", os.path.join(tmp, "jp"), say=lambda s: None); assert False, "another title's mod taken"
        except ModError as e: assert "PCSG00351" in str(e)
        assert add(W, "PCSH00143", os.path.join(tmp, "jp"), any_title=True, say=lambda s: None)[:2] == ("jp", 1)
        # only the program: nothing to add
        put("eb/eboot.bin")
        try: add(W, "PCSH00143", os.path.join(tmp, "eb"), say=lambda s: None); assert False, "a mod of only eboot.bin taken"
        except ModError: pass
        # a zip that writes outside its folder
        with zipfile.ZipFile(os.path.join(tmp, "evil.zip"), "w") as z: z.writestr("../../x.cpk", b"!")
        try: add(W, "PCSH00143", os.path.join(tmp, "evil.zip"), say=lambda s: None); assert False, "zip slip taken"
        except ModError: pass
        assert not os.path.exists(os.path.join(tmp, "x.cpk"))
        # on / off / remove; no temp folders left
        set_on(W, "m1", False)
        assert [(r[0], r[1]) for r in rows(W)] == [("English Patch", True), ("English Patch (2)", True), ("jp", True), ("m1", False)], rows(W)
        set_on(W, "m1", True); assert rows(W)[-1][1] is True
        remove(W, "English Patch (2)"); assert [r[0] for r in rows(W)] == ["English Patch", "jp", "m1"]
        assert not [e for e in os.listdir(os.path.join(W, "repatch")) if e.startswith(".mod.")]
        assert blocked("EBOOT.BIN") and blocked("sce_module/x.suprx") and blocked("a/b.self") and not blocked("data/eboot_notes.txt") and not blocked("sce_sys/param.sfo")
    print("mods.py self-test: OK")


def main(argv) -> int:
    try:
        if argv[:1] == ["selftest"]: selftest(); return 0
        if len(argv) >= 4 and argv[0] == "add":
            rest = argv[4:]; nm = rest[rest.index("--name") + 1] if "--name" in rest and rest.index("--name") + 1 < len(rest) else ""
            name, n, ref = add(argv[1], argv[2], argv[3], nm, "--any-title" in rest)
            print(f"{argv[2]}: mod {name} added ({n} files{', ' + str(len(ref)) + ' left out' if ref else ''}): on from the game's next start")
            return 0
        if len(argv) == 2 and argv[0] == "list":
            for n, on, k in rows(argv[1]): print(f"{n}\t{'on' if on else 'off'}\t{k}")
            return 0
        if len(argv) == 4 and argv[0] == "set" and argv[3] in ("on", "off"): set_on(argv[1], argv[2], argv[3] == "on"); return 0
        if len(argv) == 3 and argv[0] == "remove": remove(argv[1], argv[2]); print(f"mod {argv[2]} removed"); return 0
    except (ModError, OSError, zipfile.BadZipFile) as e:
        sys.stderr.write(f"mods: {e}\n"); return 1
    sys.stderr.write(__doc__); return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
