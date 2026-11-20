#!/usr/bin/env python3
"""A portable install end to end (RECOMPLAUNCHER-01 L2; launcher/hub.cpp portable_dir / library_dir / libmove_modal), no game:
the built launcher against made-up installs (ATIV_ROOT: a synthetic game pack, a stand-in pipeline/vitarecomp.py that notes
the folders it was given, the real pipeline/datadir.py) and a made-up user folder (HOME, APPDATA, LOCALAPPDATA,
XDG_DATA_HOME), never the real one's.

  1  "portable" at the top: library, settings, caches all in the install's folder; nothing in the user folder
  2  a library in the user folder: used from there (said on stderr), the user folder's settings copied in, nothing moved
  3  datadir.py move: the library in the folder, the user folder's gone; the launcher then uses the folder's
  4  "keep using it there" (hub.ini library_dir): that library, nothing said
  5  an update extracted over the folder: library, settings, cache kept
  6  ATIV_DUMP still wins over the marker; --data-dir wins over both; no marker: the user folder as before
  7  (Linux) an AppImage's marker beside the AppImage: that folder
  8  the dialog (a screenshot run with the install's own files, --exe's): Move / Keep / Not now

  python3 dev/test_launcher_portable.py --exe launcher/nativ          (Windows: --exe "<install>/Project Nativ.exe")
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_launcher_cli import FAKE  # noqa: E402  (the same stand-in vitarecomp)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    o = ap.parse_args()
    exe = os.path.abspath(o.exe)
    tmp = tempfile.mkdtemp(prefix="nativ-portable-")
    fails, checks = [], 0
    norm = lambda p: os.path.normcase(os.path.normpath(os.path.realpath(p)))

    def check(cond, what):
        nonlocal checks
        checks += 1
        if not cond: fails.append(what); print("FAIL", what)

    def install(at, marker=True):
        os.makedirs(os.path.join(at, "pipeline"), exist_ok=True); os.makedirs(os.path.join(at, "packs", "PCSZ00042"), exist_ok=True)
        open(os.path.join(at, "pipeline", "vitarecomp.py"), "w").write(FAKE)
        shutil.copy(os.path.join(ROOT, "pipeline", "datadir.py"), os.path.join(at, "pipeline", "datadir.py"))
        open(os.path.join(at, "packs", "PCSZ00042", "pack.toml"), "w").write('NAME = "Portable Test Game"\n')
        if marker: open(os.path.join(at, "portable"), "w").write("portable\n")
        return at

    def game_in(lib):
        w = os.path.join(lib, "decrypted", "PCSZ00042", "elf"); os.makedirs(w, exist_ok=True)
        open(os.path.join(w, "eboot.bin.elf"), "wb").write(b"\x7fELF")

    try:
        home = os.path.join(tmp, "home")
        if sys.platform == "win32": user_lib, user_prefs = os.path.join(home, "local", "ProjectNativ", "Nativ", "library"), os.path.join(home, "roaming", "ProjectNativ", "Nativ")
        else: user_lib = user_prefs = None
        if user_lib is None: user_prefs = os.path.join(home, "share", "ProjectNativ", "Nativ"); user_lib = os.path.join(user_prefs, "library")
        log = os.path.join(tmp, "calls.jsonl"); found = os.path.join(tmp, "copy"); os.makedirs(found)
        base = {k: v for k, v in os.environ.items() if k not in ("ATIV_DUMP", "ATIV_PREFS", "ATIV_TASK", "ATIV_SHOT", "ATIV_CACHE", "PYTHONPYCACHEPREFIX", "ZIG_GLOBAL_CACHE_DIR", "APPIMAGE")}
        base.update(FAKE_LOG=log, ATIV_PLAY_QUIET="1", HOME=home, USERPROFILE=home, APPDATA=os.path.join(home, "roaming"),
                    LOCALAPPDATA=os.path.join(home, "local"), XDG_DATA_HOME=os.path.join(home, "share"), XDG_CACHE_HOME=os.path.join(home, "cache"))

        def run(root, args, **env):
            if os.path.exists(log): os.remove(log)
            e = dict(base, ATIV_ROOT=root); e.update(env)
            r = subprocess.run([exe] + args, env=e, capture_output=True, text=True, timeout=120)
            first = json.loads(open(log, encoding="utf-8").readline())["env"] if os.path.isfile(log) else {}
            return r.returncode, r.stdout, r.stderr, first

        def files_under(d): return sorted(os.path.relpath(os.path.join(a, n), d) for a, _, fs in os.walk(d) for n in fs) if os.path.isdir(d) else []

        # 1 a fresh portable install
        r1 = install(os.path.join(tmp, "ProjectNativ1"))
        rc, out, err, env = run(r1, ["--add", found])
        for k, sub in (("ATIV_DUMP", "library"), ("ATIV_PREFS", "settings"), ("ATIV_CACHE", "cache"), ("PYTHONPYCACHEPREFIX", "cache/pycache"), ("ZIG_GLOBAL_CACHE_DIR", "cache/zig")):
            check(norm(env.get(k, "")) == norm(os.path.join(r1, sub)), f"1 portable: {k} = {env.get(k)!r}, want <install>/{sub}")
        check(rc == 0 and os.path.isfile(os.path.join(r1, "settings", "scan.tsv")), f"1 portable: the search's file in <install>/settings (rc {rc})")
        check(files_under(home) == [], f"1 portable: nothing in the user folder: {files_under(home)[:5]}")

        # 2 a library already in the user folder
        game_in(user_lib); os.makedirs(user_prefs, exist_ok=True)
        open(os.path.join(user_prefs, "hub.ini"), "w").write("setup_done=1\nlibrary_view=list\n")
        open(os.path.join(user_prefs, "launcher.log"), "w").write("old log\n")
        r2 = install(os.path.join(tmp, "ProjectNativ2"))
        rc, out, err, env = run(r2, ["--list"])
        check(rc == 0 and "PCSZ00042\tadded\t" in out, f"2 the user folder's library still used: rc {rc}, {out!r}")
        check("your library is still at" in err, f"2 said on stderr: {err!r}")
        check(open(os.path.join(r2, "settings", "hub.ini")).read() == "setup_done=1\nlibrary_view=list\n", "2 the user folder's settings copied in")
        check(not os.path.exists(os.path.join(r2, "settings", "launcher.log")), "2 its log not copied")
        check(os.path.isfile(os.path.join(user_prefs, "hub.ini")) and os.path.isfile(os.path.join(user_lib, "decrypted", "PCSZ00042", "elf", "eboot.bin.elf")), "2 nothing moved or removed")
        check(not os.path.exists(os.path.join(r2, "library")), "2 no empty library made beside it")
        rc, out, err, env = run(r2, ["--add", found])
        check(norm(env.get("ATIV_DUMP", "")) == norm(user_lib), f"2 the stages get that library: {env.get('ATIV_DUMP')!r}")

        # 3 moved in (the dialog's Move runs this)
        py = sys.executable
        mv = subprocess.run([py, os.path.join(r2, "pipeline", "datadir.py"), "move", user_lib, os.path.join(r2, "library")], capture_output=True, text=True)
        check(mv.returncode == 0 and not os.path.exists(user_lib), f"3 datadir.py move: rc {mv.returncode} {mv.stderr[-200:]!r}")
        rc, out, err, env = run(r2, ["--list"])
        check(rc == 0 and "PCSZ00042\tadded\t" in out and "still at" not in err, f"3 the folder's library now: rc {rc}, {out!r}, {err!r}")
        rc, out, err, env = run(r2, ["--add", found])
        check(norm(env.get("ATIV_DUMP", "")) == norm(os.path.join(r2, "library")), f"3 the stages too: {env.get('ATIV_DUMP')!r}")

        # 4 kept where it was
        game_in(user_lib)
        r3 = install(os.path.join(tmp, "ProjectNativ3")); os.makedirs(os.path.join(r3, "settings"))
        open(os.path.join(r3, "settings", "hub.ini"), "w").write(f"library_dir={user_lib}\nsetup_done=1\n")
        rc, out, err, env = run(r3, ["--add", found])
        check(rc == 0 and norm(env.get("ATIV_DUMP", "")) == norm(user_lib) and "still at" not in err, f"4 kept there: {env.get('ATIV_DUMP')!r}, {err!r}")
        check(open(os.path.join(r3, "settings", "hub.ini")).read() == f"library_dir={user_lib}\nsetup_done=1\n", "4 its settings not replaced by the user folder's")

        # 5 an update extracted over the folder (a new install tree written over it, marker included)
        new = install(os.path.join(tmp, "new-release", "ProjectNativ"))
        open(os.path.join(new, "version.txt"), "w").write("v9.9.9\n")
        before = files_under(os.path.join(r2, "library")) + files_under(os.path.join(r2, "settings"))
        shutil.copytree(new, r2, dirs_exist_ok=True)
        check(files_under(os.path.join(r2, "library")) + files_under(os.path.join(r2, "settings")) == before, "5 update: library and settings as they were")
        rc, out, err, env = run(r2, ["--list"])
        check(rc == 0 and "PCSZ00042\tadded\t" in out, f"5 update: the game still there: {out!r}")

        # 6 what wins
        other = os.path.join(tmp, "other-lib")
        rc, out, err, env = run(r2, ["--add", found], ATIV_DUMP=other)
        check(norm(env.get("ATIV_DUMP", "")) == norm(other), f"6 ATIV_DUMP over the marker: {env.get('ATIV_DUMP')!r}")
        dd = os.path.join(tmp, "data dir")
        rc, out, err, env = run(r2, ["--data-dir", dd, "--add", found], ATIV_DUMP=other)
        check(norm(env.get("ATIV_DUMP", "")) == norm(os.path.join(dd, "library")) or norm(env.get("ATIV_DUMP", "")) == norm(user_lib),
              f"6 --data-dir over both (its library, or the user folder's offered): {env.get('ATIV_DUMP')!r}")
        check(norm(env.get("ATIV_PREFS", "")) == norm(os.path.join(dd, "settings")), f"6 --data-dir: its settings: {env.get('ATIV_PREFS')!r}")
        r5 = install(os.path.join(tmp, "ProjectNativ5"), marker=False)
        rc, out, err, env = run(r5, ["--add", found])
        check(norm(env.get("ATIV_DUMP", "")) == norm(user_lib), f"6 no marker: the user folder as before: {env.get('ATIV_DUMP')!r}")
        check(not os.path.exists(os.path.join(r5, "library")) and not os.path.exists(os.path.join(r5, "settings")), "6 no marker: nothing in the install")

        # 7 an AppImage: the marker beside it
        if sys.platform.startswith("linux"):
            apps = os.path.join(tmp, "Applications"); os.makedirs(apps); open(os.path.join(apps, "portable"), "w").write("p\n")
            rc, out, err, env = run(r5, ["--add", found], APPIMAGE=os.path.join(apps, "ProjectNativ-v9-linux.AppImage"))
            check(norm(env.get("ATIV_PREFS", "")) == norm(os.path.join(apps, "settings")), f"7 AppImage marker: settings beside it: {env.get('ATIV_PREFS')!r}")
            rc, out, err, env = run(r5, ["--add", found], APPIMAGE=os.path.join(tmp, "ProjectNativ-v9-linux.AppImage"))
            check(norm(env.get("ATIV_PREFS", "")) != norm(os.path.join(tmp, "settings")), "7 AppImage without a marker beside it: not portable")

        # 8 the dialog, in a screenshot run of the real install (--exe's own folder): its buttons by controller
        shots = os.path.join(tmp, "shots"); os.makedirs(shots)
        def shot(keys, prefs):
            nav = os.path.join(shots, "nav.txt")
            if os.path.exists(nav): os.remove(nav)
            e = {k: v for k, v in base.items() if k != "ATIV_ROOT"}
            e.update(ATIV_PREFS=prefs, ATIV_DUMP=os.path.join(tmp, "shot-lib"), ATIV_SHOT=os.path.join(shots, "s.ppm"), ATIV_SHOT_UI="libmove", ATIV_SHOT_KEYS=keys, ATIV_SHOT_NAV=nav)
            subprocess.run([exe], env=e, capture_output=True, text=True, timeout=180)
            r = {"open": "", "did": []}
            if os.path.exists(nav):
                for l in open(nav, encoding="utf-8", errors="replace").read().splitlines():
                    if l.startswith("open "): r["open"] = l[5:]
                    elif l.startswith("did "): r["did"].append(l[4:])
            return r
        p8 = os.path.join(tmp, "p8"); os.makedirs(p8)
        r = shot("", p8); check("Your library" in r["open"], f"8 the dialog opens: {r}")
        r = shot("right,left,a", p8)   # (as the launcher's other dialogs: A acts once the controller has moved the cursor)
        check(any(d.startswith("libmove ") for d in r["did"]) and "Your library" not in r["open"], f"8 A on Move: the move: {r}")
        r = shot("b", p8); check(not r["did"] and "Your library" not in r["open"], f"8 B: not now, nothing done: {r}")
        r = shot("right,a", p8)
        ini = open(os.path.join(p8, "hub.ini")).read() if os.path.isfile(os.path.join(p8, "hub.ini")) else ""
        check("library_dir=" in ini and not r["did"], f"8 right, A on Keep: kept in hub.ini, no move: {r}, {ini!r}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f"launcher portable: {checks} checks, {len(fails)} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
