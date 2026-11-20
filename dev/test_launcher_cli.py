#!/usr/bin/env python3
"""The launcher's command line end to end (RECOMPLAUNCHER-01 L4; launcher/cli_args.h), no game and no window: the built
launcher run against a made-up install (ATIV_ROOT: one synthetic game pack and a stand-in pipeline/vitarecomp.py that
notes each call -- its arguments and the library / settings / cache folders it was given -- and answers a search with
one synthetic game). Checks --help, bad command lines (exit 2), --list, --play (the game's own flags passed on, an
unknown title exit 2), --build, --add, --data-dir (everything in that one folder, over ATIV_DUMP), and that ATIV_DUMP /
ATIV_TASK still work alone.

  python3 dev/test_launcher_cli.py --exe launcher/nativ          (Windows: --exe "<install>/Project Nativ.exe")
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

FAKE = r'''import json, os, sys
a = sys.argv[1:]
with open(os.environ["FAKE_LOG"], "a", encoding="utf-8") as f:
    f.write(json.dumps({"argv": a, "env": {k: os.environ.get(k, "") for k in ("ATIV_DUMP", "ATIV_PREFS", "ATIV_CACHE", "PYTHONPYCACHEPREFIX", "ZIG_GLOBAL_CACHE_DIR")}}) + "\n")
if a and a[0] == "scan":
    out = a[a.index("--out") + 1]
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        f.write("\t".join(["game", "folder", "PCSZ00043", "01.00", "recommended", "yes", "Second Test Game", a[1]]) + "\n")
print("fake vitarecomp: " + " ".join(a))
'''


def calls(log):
    if not os.path.isfile(log): return []
    return [json.loads(l) for l in open(log, encoding="utf-8")]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    o = ap.parse_args()
    exe = os.path.abspath(o.exe)
    tmp = tempfile.mkdtemp(prefix="nativ-cli-")
    fails, checks = [], 0

    def check(cond, what):
        nonlocal checks
        checks += 1
        if not cond: fails.append(what); print("FAIL", what)

    try:
        root = os.path.join(tmp, "install"); lib = os.path.join(tmp, "lib"); prefs = os.path.join(tmp, "prefs")
        os.makedirs(os.path.join(root, "pipeline")); os.makedirs(os.path.join(root, "packs", "PCSZ00042"))
        open(os.path.join(root, "pipeline", "vitarecomp.py"), "w").write(FAKE)
        open(os.path.join(root, "packs", "PCSZ00042", "pack.toml"), "w").write('NAME = "CLI Test Game"\n')
        w = os.path.join(lib, "decrypted", "PCSZ00042")
        os.makedirs(os.path.join(w, "elf")); open(os.path.join(w, "elf", "eboot.bin.elf"), "wb").write(b"\x7fELF")
        found = os.path.join(tmp, "my vita copy"); os.makedirs(found)
        log = os.path.join(tmp, "calls.jsonl")
        base = {k: v for k, v in os.environ.items() if k not in ("ATIV_DUMP", "ATIV_PREFS", "ATIV_TASK", "ATIV_SHOT", "ATIV_CACHE", "PYTHONPYCACHEPREFIX", "ZIG_GLOBAL_CACHE_DIR")}
        base.update(ATIV_ROOT=root, FAKE_LOG=log, ATIV_PLAY_QUIET="1")
        home = os.path.join(tmp, "home")   # (the usual places: a library there would be offered, never the real user's)
        base.update(HOME=home, USERPROFILE=home, APPDATA=os.path.join(home, "roaming"), LOCALAPPDATA=os.path.join(home, "local"), XDG_DATA_HOME=os.path.join(home, "share"))

        def run(args, **env):
            if os.path.exists(log): os.remove(log)
            e = dict(base, ATIV_DUMP=lib, ATIV_PREFS=prefs); e.update(env)
            for k in [k for k, v in e.items() if v is None]: del e[k]
            r = subprocess.run([exe] + args, env=e, capture_output=True, text=True, timeout=120)
            return r.returncode, r.stdout, r.stderr

        rc, out, err = run(["--help"])
        check(rc == 0 and "--data-dir" in out and "--play" in out and "--add" in out, f"--help: rc {rc}, {out[:80]!r}")
        for bad in (["--bogus"], ["--play"], ["SOMETITLE"], ["--list", "--build", "X"], ["--", "--fullscreen"], ["--list", "--windowed"]):
            rc, out, err = run(bad)
            check(rc == 2 and "usage" in err and not calls(log), f"{bad}: exit 2 with the usage, nothing run (rc {rc}, {err[:60]!r})")

        rc, out, err = run(["--list"])
        rows = [l.split("\t") for l in out.splitlines()]
        check(rc == 0 and ["PCSZ00042", "added", "-", "CLI Test Game"] in rows, f"--list: rc {rc}, {rows}")

        rc, out, err = run(["--play", "PCSZ09999"])
        check(rc == 2 and "isn't in your Project Nativ library" in err and not calls(log), f"--play unknown: rc {rc}")
        rc, out, err = run(["--play", "PCSZ00042", "--fullscreen", "--", "--windowed", "two words"])
        a = [c["argv"] for c in calls(log)]
        check(rc == 0 and a == [["ready", "PCSZ00042"], ["run", "PCSZ00042", "--", "--fullscreen", "--windowed", "two words"]], f"--play flags: rc {rc}, {a}")
        rc, out, err = run(["--play=PCSZ00042"])
        check(rc == 0 and [c["argv"] for c in calls(log)][-1] == ["run", "PCSZ00042"], "--play=<title>, no game flags")

        rc, out, err = run(["--build", "PCSZ00042"])
        a = [c["argv"][0] for c in calls(log)]
        check(rc == 0 and a == ["analyze", "recompile", "build"], f"--build: rc {rc}, {a}")
        rc, out, err = run(["--build", "PCSZ09999"])
        check(rc == 2 and not calls(log), f"--build unknown: rc {rc}")

        rc, out, err = run(["--add", found])
        a = [c["argv"] for c in calls(log)]
        check(rc == 0 and a and a[0][:2] == ["scan", found] and ["add", found, "--no-build"] in a and ["import", "PCSZ00043"] in a, f"--add: rc {rc}, {a}")
        check("adding  game\tPCSZ00043" in out, f"--add says what it adds: {out[-200:]!r}")
        rc, out, err = run(["--add", os.path.join(tmp, "nowhere")])
        check(rc == 2 and not calls(log), f"--add missing folder: rc {rc}")

        # --data-dir: library, settings, caches in one folder, over ATIV_DUMP / ATIV_PREFS; relative paths made whole
        dd = os.path.join(tmp, "portable data")
        rc, out, err = run(["--data-dir", dd, "--list"])
        norm = lambda p: os.path.normcase(os.path.normpath(p))
        check(rc == 0 and "PCSZ00042	pack	" in out and "	added	" not in out, f"--data-dir: its own (empty) library, ATIV_DUMP's game not in it: rc {rc}, {out!r}")
        if os.path.exists(os.path.join(prefs, "scan.tsv")): os.remove(os.path.join(prefs, "scan.tsv"))
        rc, out, err = run(["--data-dir", dd, "--add", found])
        env = calls(log)[0]["env"] if calls(log) else {}
        want = {"ATIV_DUMP": "library", "ATIV_PREFS": "settings", "ATIV_CACHE": "cache", "PYTHONPYCACHEPREFIX": os.path.join("cache", "pycache"), "ZIG_GLOBAL_CACHE_DIR": os.path.join("cache", "zig")}
        for k, sub in want.items():
            check(norm(env.get(k, "")) == norm(os.path.join(dd, sub)), f"--data-dir: {k} = {env.get(k)!r}, want <dd>/{sub}")
        check(os.path.isdir(os.path.join(dd, "settings")) and os.path.isfile(os.path.join(dd, "settings", "scan.tsv")), "--data-dir: the search's file in its settings")
        check(not os.path.exists(os.path.join(prefs, "scan.tsv")), "--data-dir: nothing written to ATIV_PREFS's folder")
        rel = subprocess.run([exe, "--data-dir", "rel dir", "--list"], env=dict(base), capture_output=True, text=True, cwd=tmp, timeout=60)
        check(rel.returncode == 0 and os.path.isdir(os.path.join(tmp, "rel dir")), f"--data-dir relative: made under the current folder (rc {rel.returncode})")

        # the variables alone, as before
        rc, out, err = run(["--list"], ATIV_DUMP=lib)
        check(rc == 0 and "PCSZ00042\tadded" in out, "ATIV_DUMP alone: its library")
        rc, out, err = run([], ATIV_TASK="build:PCSZ00042")
        check(rc == 0 and [c["argv"][0] for c in calls(log)] == ["analyze", "recompile", "build"], f"ATIV_TASK=build: rc {rc}")
        rc, out, err = run([], ATIV_TASK="build:PCSZ09999")
        check(rc == 2, f"ATIV_TASK unknown title: rc {rc}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f"launcher cli: {checks} checks, {len(fails)} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
