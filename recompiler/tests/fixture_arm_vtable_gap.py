#!/usr/bin/env python3
"""An ARM method that only a vtable reaches (an even pointer into a gap no body covers) becomes an ARM function
(recompiler/recomp.py, the gap pass's ARM half).

Contract (the CPU): a blx through a pointer selects the state from its bit 0 -- an even word enters ARM code.
Observed (PSN-NOVA, PCSH00143, 2026-11-18): class methods in ARM (0x8204c890 a type id, 0x8203a1ac a reference-count
increment) sit in gaps between the analysis' bodies and only vtables point at them; the gap pass took odd (Thumb)
pointers only, so each stopped a run on its first call ("neither recompiled code nor an import").

Synthetic module (no game bytes), one .text block at 0x81000000:
  F  0x81000000 Thumb: movs r0,#7 / bx lr                      (an analysis body)
  A  0x81000010 ARM:   ldr r1,[r0,#0x14] / add r1,r1,#1 / str r1,[r0,#0x14] / bx lr   (no body)
  B  0x81000020 ARM:   mov r0,#5 / bx lr                        (no body)
  vtable 0x81000030: F|1, A        -- A sits next to a code pointer: a vtable row
  0x81000040: B alone between zero words -- an even word that could be any number: not taken
PASS: fn_81000010 exists, recomp_is_arm says ARM, calling it adds 1 at r0+0x14; no fn_81000020.
Before: no fn_81000010.
Run (POSIX host with cc, e.g. WSL):  python3 recompiler/tests/fixture_arm_vtable_gap.py [--recomp <recomp.py>]
"""
import json, os, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NATIV = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
RECOMP = sys.argv[sys.argv.index("--recomp") + 1] if "--recomp" in sys.argv else os.path.join(HERE, os.pardir, "recomp.py")
h = lambda *x: b"".join(struct.pack("<H", v) for v in x)
w = lambda *x: b"".join(struct.pack("<I", v) for v in x)
B0 = 0x81000000
F, A, B = B0, B0 + 0x10, B0 + 0x20
code = bytearray(0x48)
code[0x00:0x04] = h(0x2007, 0x4770)
code[0x10:0x20] = w(0xE5901014, 0xE2811001, 0xE5801014, 0xE12FFF1E)
code[0x20:0x28] = w(0xE3A00005, 0xE12FFF1E)
code[0x30:0x38] = w(F | 1, A)
code[0x40:0x44] = w(B)
code = bytes(code)
d = {"blocks": [{"start": B0, "size": len(code), "init": True, "name": ".text", "bytes": code.hex()}],
     "functions": {"FUN_%08x" % F: F | 1}, "bodies": [[F, F + 3, F]], "relocs": [], "module": "fixture",
     "exports": [], "variables": [], "arm": []}
results = []
def check(name, ok, detail=""):
    print(("PASS  " if ok else "FAIL  ") + name + (("  -- " + detail) if detail and not ok else ""))
    results.append(ok)


work = tempfile.mkdtemp(prefix="fixture_avg_")
js = os.path.join(work, "m.data.json"); json.dump(d, open(js, "w"))
r = subprocess.run([sys.executable, RECOMP, js, os.path.join(work, "m.c"), "--split", "1"], capture_output=True, text=True)
if r.returncode: print("recomp failed:", (r.stdout + r.stderr)[-600:]); sys.exit(2)
src = "".join(open(os.path.join(work, x)).read() for x in os.listdir(work) if x.endswith(".c"))
check("the ARM method A (vtable row) is a function", ("void fn_%08x(Cpu *c)" % A) in src)
check("the lone even word B is not taken", ("void fn_%08x(Cpu *c)" % B) not in src)
cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
if ("void fn_%08x(Cpu *c)" % A) not in src:
    pass
elif os.name != "posix" or not cc:
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
        f.write("\tstatic Cpu c; memset(&c, 0, sizeof c); c.r[13] = 0x100000; c.r[0] = 0x200000;\n")
        f.write("\t*(uint32_t *)(recomp_mem + 0x200014) = 41; fn_%08x(&c);\n" % A)
        f.write("\tprintf(\"%%u %%d %%d\\n\", *(uint32_t *)(recomp_mem + 0x200014), recomp_is_arm(0x%xu), recomp_is_arm(0x%xu));\n" % (A, F))
        f.write("\treturn 0;\n}\n")
    exe = os.path.join(work, "run")
    srcs = [os.path.join(work, x) for x in sorted(os.listdir(work)) if x.endswith(".c")]
    r = subprocess.run([cc, "-O2", "-w", "-I", os.path.join(NATIV, "recompiler"), "-o", exe] + srcs + ["-lm"], capture_output=True, text=True)
    if r.returncode:
        check("generated C compiles", False, r.stderr[-800:])
    else:
        out = subprocess.run([exe], capture_output=True, text=True).stdout.split()
        check("calling A adds 1 at r0+0x14 (41 -> 42)", out[:1] == ["42"], str(out))
        check("recomp_is_arm: A is ARM, F (Thumb) is not", out[1:3] == ["1", "0"], str(out))
shutil.rmtree(work, ignore_errors=True)
failed = results.count(False)
print(f"\n{len(results) - failed}/{len(results)} checks passed")
sys.exit(0 if failed == 0 else 1)
