"""The player's library: games, firmware, licences, builds and saves (the folder the code calls the dump).

ATIV_DUMP=<dir> when set (a test library, a second disk); else <root>/dump when it exists (a checkout, or an install
from before the library had a folder of its own: nothing moves); else the platform's per-user data folder, beside the
launcher's settings, so an update can replace the install without touching anything the player made:
  macOS    ~/Library/Application Support/ProjectNativ/Nativ/library
  Linux    $XDG_DATA_HOME/ProjectNativ/Nativ/library   (~/.local/share when unset)
  Windows  %LOCALAPPDATA%/ProjectNativ/Nativ/library    (local rather than roaming: a game is gigabytes)
The launcher makes the same choice (launcher/hub.cpp library_dir) and passes it to the stages as ATIV_DUMP.
A portable install (a file named "portable" in it, or the launcher's --data-dir) keeps the library in <folder>/library
instead; the launcher then offers to move a library found in the usual place there (RECOMPLAUNCHER-01):

  python3 pipeline/datadir.py move <library> <new folder>     the whole library moved: renamed on the same drive, else
                                                             copied, checked (every file, its size), and only then the
                                                             old one removed; links (a copy read in place, a Windows
                                                             junction) stay links. Fails with nothing lost: the old one
                                                             kept, a part-made copy removed. The new folder must be
                                                             missing or empty.
  python3 pipeline/datadir.py selftest
"""
import os
import shutil
import sys


def library_dir(root: str) -> str:
    d = os.environ.get("ATIV_DUMP")
    if d: return d
    if os.path.isdir(os.path.join(root, "dump")): return os.path.join(root, "dump")
    home = os.path.expanduser("~")
    if sys.platform == "win32": base = os.environ.get("LOCALAPPDATA") or os.path.join(home, "AppData", "Local")
    elif sys.platform == "darwin": base = os.path.join(home, "Library", "Application Support")
    else: base = os.environ.get("XDG_DATA_HOME") or os.path.join(home, ".local", "share")
    return os.path.join(base, "ProjectNativ", "Nativ", "library")


def prefs_dir() -> str:
    """the launcher's own folder (SDL_GetPrefPath("ProjectNativ", "Nativ"), or ATIV_PREFS): its settings, the settings every
    game reads (settings.ini) and its log (launcher.log). Windows keeps it in the roaming folder, as SDL does"""
    d = os.environ.get("ATIV_PREFS")
    if d: return d
    home = os.path.expanduser("~")
    if sys.platform == "win32": base = os.environ.get("APPDATA") or os.path.join(home, "AppData", "Roaming")
    elif sys.platform == "darwin": base = os.path.join(home, "Library", "Application Support")
    else: base = os.environ.get("XDG_DATA_HOME") or os.path.join(home, ".local", "share")
    return os.path.join(base, "ProjectNativ", "Nativ")


# ---- moving a library (RECOMPLAUNCHER-01 L2)
def _is_link(p):
    return os.path.islink(p) or (hasattr(os.path, "isjunction") and os.path.isjunction(p))


def _copy_tree(src, dst, stats):
    os.makedirs(dst)
    for e in sorted(os.listdir(src)):
        s, d = os.path.join(src, e), os.path.join(dst, e)
        if hasattr(os.path, "isjunction") and os.path.isjunction(s):
            import _winapi   # (a Windows junction: the same target again, never its contents)
            _winapi.CreateJunction(os.path.realpath(s), d)
        elif os.path.islink(s):
            os.symlink(os.readlink(s), d, target_is_directory=os.path.isdir(s))
        elif os.path.isdir(s):
            _copy_tree(s, d, stats)
        else:
            shutil.copy2(s, d)
            stats[0] += 1; stats[1] += os.path.getsize(s)
            if stats[0] % 500 == 0: print(f"  {stats[0]} files, {stats[1] >> 20} MB", flush=True)


def _listing(root):   # every entry: its kind and (a file) its size; a link its target
    out = {}
    for d, dirs, files in os.walk(root):
        for n in dirs + files:
            p = os.path.join(d, n); r = os.path.relpath(p, root)
            out[r] = ("link", os.readlink(p) if os.path.islink(p) else os.path.realpath(p)) if _is_link(p) else ("dir",) if os.path.isdir(p) else ("file", os.path.getsize(p))
        dirs[:] = [n for n in dirs if not _is_link(os.path.join(d, n))]
    return out


def _remove_tree(p):   # never through a link: a link (a copy read in place) is removed itself, its target untouched
    if _is_link(p):
        try: os.unlink(p)
        except (IsADirectoryError, PermissionError, OSError): os.rmdir(p)
        return
    if not os.path.isdir(p): os.remove(p); return
    for e in os.listdir(p): _remove_tree(os.path.join(p, e))
    os.rmdir(p)


def move_library(src, dst):
    src, dst = os.path.abspath(src), os.path.abspath(dst)
    if not os.path.isdir(src): print(f"no library at {src}", file=sys.stderr); return 2
    if os.path.normcase(dst).startswith(os.path.normcase(src) + os.sep): print("the new folder is inside the library", file=sys.stderr); return 2
    if os.path.exists(dst) and (not os.path.isdir(dst) or os.listdir(dst)): print(f"{dst} isn't empty: nothing moved", file=sys.stderr); return 2
    if os.path.isdir(dst): os.rmdir(dst)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    try:
        os.rename(src, dst); print(f"moved {src} -> {dst}"); return 0   # (the same drive: one step)
    except OSError:
        pass
    print(f"copying {src} -> {dst} (another drive)", flush=True)
    stats = [0, 0]
    try:
        _copy_tree(src, dst, stats)
        a, b = _listing(src), _listing(dst)
        if a != b:
            bad = sorted(set(a.items()) ^ set(b.items()))[:5]
            raise OSError(f"the copy differs from the library: {bad}")
    except (OSError, shutil.Error) as e:
        print(f"couldn't move the library: {e}; it stays at {src}", file=sys.stderr)
        if os.path.isdir(dst): _remove_tree(dst)
        return 1
    _remove_tree(src)
    print(f"moved {stats[0]} files ({stats[1] >> 20} MB) -> {dst}")
    return 0


def selftest():
    import tempfile
    t = tempfile.mkdtemp(prefix="datadir-test-")
    try:
        src, out = os.path.join(t, "old", "library"), os.path.join(t, "outside")
        os.makedirs(os.path.join(src, "decrypted", "PCSZ00042", "elf")); os.makedirs(out)
        open(os.path.join(src, "decrypted", "PCSZ00042", "elf", "eboot.bin.elf"), "wb").write(b"x" * 1000)
        open(os.path.join(out, "keep.bin"), "wb").write(b"mine")
        try: os.symlink(out, os.path.join(src, "decrypted", "PCSZ00042", "data"), target_is_directory=True); link = True
        except OSError:   # (Windows without the right to make symbolic links: a junction, as an in-place copy has there)
            try: import _winapi; _winapi.CreateJunction(out, os.path.join(src, "decrypted", "PCSZ00042", "data")); link = True
            except (ImportError, OSError): link = False
        before = _listing(src)
        busy = os.path.join(t, "busy"); os.makedirs(busy); open(os.path.join(busy, "x"), "w").write("x")
        assert move_library(src, busy) == 2 and _listing(src) == before, "a folder in use: refused, nothing changed"
        dst = os.path.join(t, "new", "library")
        assert move_library(src, dst) == 0 and not os.path.exists(src) and _listing(dst) == before
        assert os.path.isfile(os.path.join(out, "keep.bin")), "a link's target untouched"
        if link: assert _is_link(os.path.join(dst, "decrypted", "PCSZ00042", "data"))
        # the copy path (another drive): forced, then checked the same; a removal through the link must not happen
        dst2 = os.path.join(t, "copy", "library")
        real = os.rename
        def no_rename(a, b): raise OSError(18, "cross-device")
        os.rename = no_rename
        try: assert move_library(dst, dst2) == 0
        finally: os.rename = real
        assert not os.path.exists(dst) and _listing(dst2) == before and os.path.isfile(os.path.join(out, "keep.bin"))
        print("datadir.py self-test: OK" + ("" if link else " (links skipped)"))
        return 0
    finally:
        shutil.rmtree(t, ignore_errors=True)


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["move"] and len(a) == 3: sys.exit(move_library(a[1], a[2]))
    if a[:1] == ["selftest"]: sys.exit(selftest())
    print(__doc__, file=sys.stderr); sys.exit(2)
