#!/usr/bin/env python3
"""EXPORT-APK A2: runtime/build.py's Android targets (TARGET=android-arm64 | android-x86_64).

  python3 dev/test_build_android.py        (Linux or WSL; the link checks need runtime/platform/android/setup-toolchain.sh's
                                            toolchain, ATIV_ANDROID_TOOLCHAIN or ~/toolchains/ativ-android: skipped without)

A synthetic module (one Thumb function: bx lr) recompiled by recomp.py and built with the whole runtime for each ABI:
<out folder>/android-<abi>/libmain.so -- a shared library for that machine that exports SDL_main and names no
undefined symbol (--no-undefined), libSDL3.so beside it. A wrong TARGET and a missing toolchain stop with their reason.
"""
import json, os, shutil, struct, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "runtime", "build.py"); RECOMP = os.path.join(ROOT, "recompiler", "recomp.py")
AT = os.environ.get("ATIV_ANDROID_TOOLCHAIN") or os.path.expanduser("~/toolchains/ativ-android")
checks = fails = 0


def check(what, ok, detail=""):
    global checks, fails
    checks += 1; fails += not ok
    print(f"{what:<78} {'ok' if ok else 'FAILED'}{('  (' + detail + ')') if detail and not ok else ''}")


def build(env, base, out):
    e = dict(os.environ); e.update(env); e.setdefault("JOBS", str(os.cpu_count() or 2))
    r = subprocess.run([sys.executable, BUILD, base, out], env=e, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def elf(path):   # (e_type, e_machine)
    h = open(path, "rb").read(20)
    return struct.unpack_from("<HH", h, 16) if h[:4] == b"\x7fELF" else (0, 0)


with tempfile.TemporaryDirectory() as tmp:
    rc, log = build({"TARGET": "android-arm7"}, os.path.join(tmp, "game"), os.path.join(tmp, "game_host"))
    check("TARGET=android-arm7 stops and names the two Android targets", rc == 1 and "android-arm64 or android-x86_64" in log, log[-200:])
    rc, log = build({"TARGET": "android-arm64", "ATIV_ANDROID_TOOLCHAIN": os.path.join(tmp, "none")}, os.path.join(tmp, "game"), os.path.join(tmp, "game_host"))
    check("a missing Android toolchain stops with the script to run", rc == 1 and "setup-toolchain.sh" in log and "no Android toolchain" in log, log[-200:])

    if not os.path.isfile(os.path.join(AT, "arm64-v8a", "SDL3", "lib", "libSDL3.so")):
        print(f"(no Android toolchain at {AT}: the link checks are skipped)")
    else:
        code = bytes.fromhex("7047")   # bx lr
        start = 0x81000000
        d = {"blocks": [{"start": start, "size": len(code), "init": True, "bytes": code.hex()},
                        {"start": 0x81000100, "size": 4, "init": True, "bytes": struct.pack("<I", start | 1).hex()}],
             "functions": {"FUN_81000000": start}, "bodies": [[start, start + len(code) - 1]],
             "relocs": [], "module": "fixture", "exports": [], "variables": [], "arm": []}
        js = os.path.join(tmp, "m.json"); json.dump(d, open(js, "w"))
        src = os.path.join(tmp, "src"); os.makedirs(src)
        r = subprocess.run([sys.executable, RECOMP, js, os.path.join(src, "game.c"), "--split", "2"], capture_output=True, text=True)
        check("recomp.py makes the synthetic module's C", r.returncode == 0 and os.path.isfile(os.path.join(src, "game.c")), (r.stdout + r.stderr)[-300:])
        R = os.path.join(AT, "ndk", "toolchains", "llvm", "prebuilt", "linux-x86_64", "bin", "llvm-readelf")
        for target, abi, machine in (("android-arm64", "arm64-v8a", 0xB7), ("android-x86_64", "x86_64", 0x3E)):
            rc, log = build({"TARGET": target}, os.path.join(src, "game"), os.path.join(tmp, "out", "game_host"))
            so = os.path.join(tmp, "out", "android-" + abi, "libmain.so")
            check(f"{target}: build.py exits 0 and writes android-{abi}/libmain.so", rc == 0 and os.path.isfile(so), log[-600:])
            if not os.path.isfile(so): continue
            check(f"{target}: libmain.so is a shared library for that machine", elf(so) == (3, machine), str(elf(so)))
            check(f"{target}: libSDL3.so beside it", os.path.isfile(os.path.join(os.path.dirname(so), "libSDL3.so")))
            dyn = subprocess.run([R, "--dyn-syms", "-W", so], capture_output=True, text=True).stdout
            check(f"{target}: SDL_main exported (SDL's Java side starts it)", any(l.split()[-1:] == ["SDL_main"] and " UND " not in l for l in dyn.splitlines()))
            dynamic = subprocess.run([R, "-d", "-lW", so], capture_output=True, text=True).stdout
            check(f"{target}: 16 KB page alignment, no text relocations", "0x4000" in dynamic and "TEXTREL" not in dynamic)
            check(f"{target}: its objects in obj_android_{abi}/", os.path.isdir(os.path.join(src, "obj_android_" + abi)))

print(f"{checks} checks, {'all passed' if not fails else str(fails) + ' failed'}")
sys.exit(1 if fails else 0)
