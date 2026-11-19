#!/usr/bin/env python3
"""A function that certain Thumb code reaches with a plain branch (b, b<cc>, b.w: a tail call) stays Thumb, whatever its
own decode looks like (recompiler/recomp.py, the ARM-mode classification).

Contract (the CPU): B never changes the instruction set -- only BX/BLX (and loads to pc) interwork. A Thumb branch
lands in Thumb code, exactly like a Thumb BL.
Observed (PSN-NOVA-JP, PCSG00351 1.05, 2026-11-19): a function whose analysis chunks start below its entry was decoded
as Thumb from its first chunk; that decode held a coprocessor op, the "maybe ARM" guess made it ARM, and its only
direct reference was a Thumb `bhs.w` to its entry -- the build then failed ("use of undeclared label").

Synthetic module (no game bytes), one .text block at 0x81000000:
  C  0x81000000 Thumb: b #0x81000040                      (an analysis body)
  T  chunk 0x81000020: stc p5, c0, [r0]  (Thumb-2 bytes ed80 0500: a coprocessor op no Vita code has)
     entry 0x81000040: movs r0,#7 / bx lr                (T's analysis body: both chunks, owner T)
PASS: recomp_is_arm(T) is 0 and calling fn_81000040 returns r0 = 7.
Before: T classified ARM (recomp_is_arm 1, its body ARM garbage).
Run (POSIX host with cc, e.g. WSL):  python3 recompiler/tests/fixture_thumb_b_keeps_mode.py [--recomp <recomp.py>]
"""
import json, os, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NATIV = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
RECOMP = sys.argv[sys.argv.index("--recomp") + 1] if "--recomp" in sys.argv else os.path.join(HERE, os.pardir, "recomp.py")
h = lambda *x: b"".join(struct.pack("<H", v) for v in x)
B0 = 0x81000000
C, T = B0, B0 + 0x40
code = bytearray(0x48)
code[0x00:0x02] = h(0xE01E)                    # b #0x81000040
code[0x20:0x24] = h(0xED80, 0x0500)            # stc p5, c0, [r0]
code[0x40:0x44] = h(0x2007, 0x4770)            # movs r0, #7 ; bx lr
code = bytes(code)
d = {"blocks": [{"start": B0, "size": len(code), "init": True, "name": ".text", "bytes": code.hex()}],
     "functions": {"FUN_%08x" % C: C | 1, "FUN_%08x" % T: T | 1},
     "bodies": [[C, C + 1, C], [B0 + 0x20, B0 + 0x23, T], [T, T + 3, T]], "relocs": [], "module": "fixture",
     "exports": [], "variables": [], "arm": []}
results = []
def check(name, ok, detail=""):
    print(("PASS  " if ok else "FAIL  ") + name + (("  -- " + detail) if detail and not ok else ""))
    results.append(ok)


work = tempfile.mkdtemp(prefix="fixture_tbm_")
js = os.path.join(work, "m.data.json"); json.dump(d, open(js, "w"))
r = subprocess.run([sys.executable, RECOMP, js, os.path.join(work, "m.c"), "--split", "1"], capture_output=True, text=True)
if r.returncode: print("recomp failed:", (r.stdout + r.stderr)[-600:]); sys.exit(2)
src = "".join(open(os.path.join(work, x)).read() for x in os.listdir(work) if x.endswith(".c"))
check("T is a function", ("void fn_%08x(Cpu *c)" % T) in src)
check("T's body is its Thumb decode (movs r0, #7)", "movs r0, #7" in src)
cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
if os.name != "posix" or not cc:
    print("SKIP  run (needs a POSIX host with cc)")
else:
    with open(os.path.join(work, "run.c"), "w") as f:
        f.write('#include "m.h"\n#include <stdio.h>\n#include <string.h>\n#include <sys/mman.h>\n')
        f.write("uint8_t *recomp_mem; uint32_t recomp_watch_lo, recomp_watch_hi;\n")
        f.write("void recomp_watch_write(uint32_t a, uint32_t v, int w) {}\n")
        f.write("void recomp_call_external(Cpu *c, uint32_t a) { c->r[0] = 1000; }\n")
        f.write("void recomp_unimplemented(Cpu *c, uint32_t a) { c->r[0] = 2000; }\n")
        f.write("RecompJmp *recomp_setjmp_begin(Cpu *c) { return 0; }\n")
        f.write("int recomp_is_arm(uint32_t a);\n")
        f.write("int main(void) {\n\trecomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);\n")
        f.write("\t{ static const uint8_t b[] = {%s}; memcpy(recomp_mem + 0x%xu, b, sizeof b); }\n" % (",".join(str(x) for x in code), B0))
        f.write("\tstatic Cpu c; memset(&c, 0, sizeof c); c.r[13] = 0x100000; c.r[14] = 0x81000101; fn_%08x(&c);\n" % T)
        f.write("\tprintf(\"%%u %%d\\n\", c.r[0], recomp_is_arm(0x%xu));\n" % T)
        f.write("\treturn 0;\n}\n")
    exe = os.path.join(work, "run")
    srcs = [os.path.join(work, x) for x in sorted(os.listdir(work)) if x.endswith(".c")]
    r = subprocess.run([cc, "-O2", "-w", "-I", os.path.join(NATIV, "recompiler"), "-o", exe] + srcs + ["-lm"], capture_output=True, text=True)
    if r.returncode:
        check("generated C compiles", False, r.stderr[-800:])
    else:
        out = subprocess.run([exe], capture_output=True, text=True).stdout.split()
        check("calling T returns r0 = 7", out[:1] == ["7"], str(out))
        check("recomp_is_arm(T) is 0 (Thumb)", out[1:2] == ["0"], str(out))
shutil.rmtree(work, ignore_errors=True)
failed = results.count(False)
print(f"\n{len(results) - failed}/{len(results)} checks passed")
sys.exit(0 if failed == 0 else 1)
