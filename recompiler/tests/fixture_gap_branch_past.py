#!/usr/bin/env python3
"""A gap function (reached only through a pointer, no analysis body) whose own branch jumps past its first
unconditional b keeps the code behind it (recompiler/recomp.py, gap_end: both gap passes).

Contract (the CPU): a branch inside a function lands on that function's own code; the function does not end at a b
when an earlier branch of it targets an address further on.
Observed (PSN-NOVA, PCSH00143, 2026-11-18, owner run 2): an ARM gap method "cmp r1,#0; beq ...; ...; b ..." with its
switch cases after the b was cut at the b, so each case was called as an external and a run stopped on the first
("neither recompiled code nor an import").

Synthetic module (no game bytes), one .text block at 0x81000000:
  F  0x81000000 Thumb: movs r0,#7 / bx lr                                                    (an analysis body)
  S  0x81000010 ARM:   cmp r1,#0 / beq L2 / mov r0,#1 / b L3 / L2: mov r0,#2 / L3: bx lr     (no body)
  T  0x81000030 Thumb: cmp r1,#0 / beq L2 / movs r0,#1 / b L3 / L2: movs r0,#2 / L3: bx lr   (no body)
  vtable 0x81000040: F|1, S, T|1, U|1
  G  0x81000050 Thumb: movs r0,#0 / bx lr                                                    (an analysis body: the gap's end)
  U  0x81000054 Thumb: b 0x81000058 / movs r0,#3 / movs r0,#4 / movs r0,#5  (no body; no return after the b's
     target before H) -- a lone b trampoline, a gap function before this fix too: it must stay one
  H  0x8100005c Thumb: movs r0,#0 / bx lr                                                    (an analysis body)
PASS: S and T return 2 for r1 = 0 and 1 for r1 = 1 (the stub for an external call returns 1000).
Before: the code after the first b is cut off -- S(0) and T(0) return 1000 (L2 external).
Run (POSIX host with cc, e.g. WSL):  python3 recompiler/tests/fixture_gap_branch_past.py [--recomp <recomp.py>]
"""
import json, os, re, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NATIV = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
RECOMP = sys.argv[sys.argv.index("--recomp") + 1] if "--recomp" in sys.argv else os.path.join(HERE, os.pardir, "recomp.py")
h = lambda *x: b"".join(struct.pack("<H", v) for v in x)
w = lambda *x: b"".join(struct.pack("<I", v) for v in x)
B0 = 0x81000000
F, S, T, G, U, H = B0, B0 + 0x10, B0 + 0x30, B0 + 0x50, B0 + 0x54, B0 + 0x5c
code = bytearray(0x60)
code[0x00:0x04] = h(0x2007, 0x4770)
code[0x10:0x28] = w(0xE3510000, 0x0A000001, 0xE3A00001, 0xEA000000, 0xE3A00002, 0xE12FFF1E)
code[0x30:0x3c] = h(0x2900, 0xD001, 0x2001, 0xE000, 0x2002, 0x4770)
code[0x40:0x50] = w(F | 1, S, T | 1, U | 1)
code[0x50:0x54] = h(0x2000, 0x4770)
code[0x54:0x5c] = h(0xE000, 0x2003, 0x2004, 0x2005)
code[0x5c:0x60] = h(0x2000, 0x4770)
code = bytes(code)
d = {"blocks": [{"start": B0, "size": len(code), "init": True, "name": ".text", "bytes": code.hex()}],
     "functions": {"FUN_%08x" % F: F | 1, "FUN_%08x" % G: G | 1, "FUN_%08x" % H: H | 1}, "bodies": [[F, F + 3, F], [G, G + 3, G], [H, H + 3, H]], "relocs": [], "module": "fixture",
     "exports": [], "variables": [], "arm": []}
results = []
def check(name, ok, detail=""):
    print(("PASS  " if ok else "FAIL  ") + name + (("  -- " + detail) if detail and not ok else ""))
    results.append(ok)


work = tempfile.mkdtemp(prefix="fixture_gbp_")
js = os.path.join(work, "m.data.json"); json.dump(d, open(js, "w"))
r = subprocess.run([sys.executable, RECOMP, js, os.path.join(work, "m.c"), "--split", "1"], capture_output=True, text=True)
if r.returncode: print("recomp failed:", (r.stdout + r.stderr)[-600:]); sys.exit(2)
src = "".join(open(os.path.join(work, x)).read() for x in os.listdir(work) if x.endswith(".c"))
have = all(("void fn_%08x(Cpu *c)" % e) in src for e in (S, T))
check("U (a lone b, no return past its target) stays a gap function", ("void fn_%08x(Cpu *c)" % U) in src)
check("S (ARM) and T (Thumb) are gap functions", have, " ".join(sorted(set(re.findall(r"void fn_[0-9a-f]+", src)))))
cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
if not have:
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
        f.write("int main(void) {\n\trecomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);\n")
        f.write("\t{ static const uint8_t b[] = {%s}; memcpy(recomp_mem + 0x%xu, b, sizeof b); }\n" % (",".join(str(x) for x in code), B0))
        f.write("\tstatic Cpu c;\n")
        for e in (S, T):
            for r1 in (0, 1):
                f.write("\tmemset(&c, 0, sizeof c); c.r[13] = 0x100000; c.r[1] = %d; fn_%08x(&c); printf(\"%%u \", c.r[0]);\n" % (r1, e))
        f.write("\tprintf(\"\\n\");\n\treturn 0;\n}\n")
    exe = os.path.join(work, "run")
    srcs = [os.path.join(work, x) for x in sorted(os.listdir(work)) if x.endswith(".c")]
    r = subprocess.run([cc, "-O2", "-w", "-I", os.path.join(NATIV, "recompiler"), "-o", exe] + srcs + ["-lm"], capture_output=True, text=True)
    if r.returncode:
        check("generated C compiles", False, r.stderr[-800:])
    else:
        out = subprocess.run([exe], capture_output=True, text=True).stdout.split()
        check("S (ARM): r1 0 -> 2 (L2 after the b), r1 1 -> 1", out[0:2] == ["2", "1"], str(out))
        check("T (Thumb): r1 0 -> 2 (L2 after the b), r1 1 -> 1", out[2:4] == ["2", "1"], str(out))
shutil.rmtree(work, ignore_errors=True)
failed = results.count(False)
print(f"\n{len(results) - failed}/{len(results)} checks passed")
sys.exit(0 if failed == 0 else 1)
