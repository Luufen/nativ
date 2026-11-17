#!/usr/bin/env python3
"""EXPORT-APK A3: pipeline/apk.py -- the package id, manifest, launch file and a real APK made with the Android toolchain.

  python3 dev/test_export_apk.py     (Linux or WSL; the APK checks need runtime/platform/android/setup-toolchain.sh's
                                      toolchain, ATIV_ANDROID_TOOLCHAIN or ~/toolchains/ativ-android: skipped without)

The APK is built from the toolchain's own check library (check/libmain-arm64-v8a.so, a stand-in for a game's) and made-up
assets; nothing of a game is used.
"""
import os, re, subprocess, sys, tempfile, zipfile
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pipeline"))
import apk  # noqa: E402

checks = fails = 0
A = "{http://schemas.android.com/apk/res/android}"


def check(what, ok, detail=""):
    global checks, fails
    checks += 1; fails += not ok
    print(f"{what:<80} {'ok' if ok else 'FAILED'}{('  (' + str(detail)[-300:] + ')') if detail and not ok else ''}")


def raises(f, *a):
    try: f(*a); return False
    except ValueError: return True


check("package id: local.nativ.<title id in lower case>", apk.package_id("PCSZ00042") == "local.nativ.pcsz00042")
check("package id: anything but a title id is refused", raises(apk.package_id, "../x") and raises(apk.package_id, "pcsz00042") and raises(apk.package_id, ""))
m = ET.fromstring(apk.manifest_text("local.nativ.pcsz00042", 'Game & "Two" <X>', 3, "1.00"))
act = m.find("application/activity")
check("manifest: package, version, min SDK 29", m.get("package") == "local.nativ.pcsz00042" and m.get(A + "versionCode") == "3"
      and m.get(A + "versionName") == "1.00" and m.find("uses-sdk").get(A + "minSdkVersion") == "29")
check("manifest: the label as given (& \" < > escaped in the file)", m.find("application").get(A + "label") == 'Game & "Two" <X>')
check("manifest: NativActivity launches it, landscape", act.get(A + "name") == apk.ACTIVITY and act.get(A + "screenOrientation") == "sensorLandscape"
      and act.find("intent-filter/category").get(A + "name") == "android.intent.category.LAUNCHER")
lt = apk.launch_text(["$FILES/game.img", "81000000", "100000000000"], [("RECOMP_TITLE", "PCSZ00042"), ("A", "x=y")])
check("launch.txt: an arg line per argument, an env line per setting", lt == "arg $FILES/game.img\narg 81000000\narg 100000000000\nenv RECOMP_TITLE=PCSZ00042\nenv A=x=y\n", lt)
check("launch.txt: a line break in a value or a bad name is refused",
      raises(apk.launch_text, ["a\nb"], []) and raises(apk.launch_text, [], [("A B", "1")]) and raises(apk.launch_text, [], [("A", "1\n2")]))
java = open(os.path.join(ROOT, "runtime", "platform", "android", "NativActivity.java"), encoding="utf-8").read()
check("NativActivity reads launch.txt's arg / env lines and names the data folder",
      'startsWith("arg ")' in java and 'startsWith("env ")' in java and "NATIV_DATA" in java and "extends SDLActivity" in java)

try: t = apk.toolchain()
except RuntimeError as e: t = None; print(f"({e}: the APK checks are skipped)")
if t:
    stand_in = os.path.join(t["root"], "check", "libmain-arm64-v8a.so")
    with tempfile.TemporaryDirectory() as tmp:
        lib = os.path.join(tmp, "arm64-v8a"); os.makedirs(lib)
        for src, n in ((stand_in, "libmain.so"), (os.path.join(t["root"], "arm64-v8a", "SDL3", "lib", "libSDL3.so"), "libSDL3.so")):
            with open(src, "rb") as s, open(os.path.join(lib, n), "wb") as d: d.write(s.read())
        img = os.path.join(tmp, "game.img"); open(img, "wb").write(os.urandom(4096))
        launch = os.path.join(tmp, "launch.txt"); open(launch, "w").write(lt)
        stamp = os.path.join(tmp, "stamp.txt"); open(stamp, "w").write("1\n")
        icon = os.path.join(ROOT, "assets", "brand", "png", "icon-256.png")
        ks = os.path.join(tmp, "key", "export.keystore"); out = os.path.join(tmp, "out", "game.apk")
        kw = dict(package="local.nativ.pcsz00001", label="Test & Game", libs={"arm64-v8a": [os.path.join(lib, "libSDL3.so"), os.path.join(lib, "libmain.so")]},
                  assets=[(img, "game.img"), (launch, "launch.txt"), (stamp, "stamp.txt")], icon=icon, keystore=ks, t=t,
                  java_src=os.path.join(ROOT, "runtime", "platform", "android", "NativActivity.java"), version_name="1.00")
        try: apk.build_apk(out, **kw); err = ""
        except subprocess.CalledProcessError as e: err = str(e)
        check("build_apk: an APK made, signed and verified (apksigner verify)", os.path.isfile(out) and not err, err)
        if os.path.isfile(out):
            names = zipfile.ZipFile(out).namelist()
            check("APK: classes.dex, both libraries for arm64-v8a, the assets",
                  {"classes.dex", "lib/arm64-v8a/libmain.so", "lib/arm64-v8a/libSDL3.so", "assets/nativ/game.img",
                   "assets/nativ/launch.txt", "assets/nativ/stamp.txt", "AndroidManifest.xml"} <= set(names), names)
            check("APK: no .idsig left beside it", not os.path.exists(out + ".idsig"))
            dex = zipfile.ZipFile(out).read("classes.dex")
            check("classes.dex: NativActivity and SDL's SDLActivity", b"Lorg/projectnativ/app/NativActivity;" in dex and b"Lorg/libsdl/app/SDLActivity;" in dex)
            b = subprocess.run([t["aapt2"], "dump", "badging", out], capture_output=True, text=True).stdout
            check("aapt2 badging: package, label, SDK 29, launchable NativActivity, native code arm64-v8a",
                  "package: name='local.nativ.pcsz00001'" in b and "application-label:'Test & Game'" in b and re.search(r"(?m)^(min)?[sS]dkVersion:'29'", b)
                  and f"launchable-activity: name='{apk.ACTIVITY}'" in b and "native-code: 'arm64-v8a'" in b, b[:600])
            env = dict(os.environ, JAVA_HOME=t["java_home"], PATH=os.path.join(t["java_home"], "bin") + os.pathsep + os.environ.get("PATH", ""))
            cert = lambda p: re.findall(r"SHA-256 digest: (\w+)", subprocess.run([t["apksigner"], "verify", "--print-certs", p], capture_output=True, text=True, env=env).stdout)
            c1, kt = cert(out), os.path.getmtime(ks)
            out2 = os.path.join(tmp, "out", "again.apk"); apk.build_apk(out2, **kw)
            check("the key is made once: a second APK is signed with the same certificate", c1 and cert(out2) == c1 and os.path.getmtime(ks) == kt, (c1, cert(out2)))

print(f"{checks} checks, {'all passed' if not fails else str(fails) + ' failed'}")
sys.exit(1 if fails else 0)
