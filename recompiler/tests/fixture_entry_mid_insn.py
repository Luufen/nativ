#!/usr/bin/env python3
"""An entry that lands inside an instruction of its own function's linear decode gets that entry's code
(recompiler/recomp.py Fn.entry_prefix).

Contract (the CPU): a call to an address runs the instructions decoded FROM that address -- for Thumb, a call to the
second halfword of a 32-bit instruction runs that halfword as its own instruction.
Observed (PSN-NOVA, PCSH00143, 2026-11-18): the analysis gives entry 0x81b688cc a chunk [0x81b68422, 0x81b688cd] that
overlaps its own chunk at 0x81b688cc; recomp.py merges them into one span that starts before the entry, the linear
decode reads a 4-byte ldr.w at 0x81b688ca, and fn_81b688cc's "goto L_81b688cc" names a label nothing emitted:
clang "use of undeclared label", the build stops.

Synthetic Thumb module (no game bytes), F at 0x81000000, E = F + 4 (inside mov.w):
  F+0 movs r0,#5 / F+2 mov.w r2,#0 (F04F 0200) / F+6 adds r0,#1 / F+8 bx lr
  read from E: 0x0200 = lsls r0,r0,#8, then adds r0,#1 / bx lr (the body's decode again at F+6)
  bodies: F [F, F+9]; E [F, F+5] (overlaps E's own chunk) and [E, F+9] -- E's spans merge into [F, F+9]
PASS: the generated C compiles; fn_F returns r0 6 r2 0; fn_E with r0 = 1 returns r0 257 (lsls #8, adds #1) and leaves
r2 alone (mov.w not run). Before the fix: the C does not compile (undeclared label L_81000004).
Run (POSIX host with cc, e.g. WSL):  python3 recompiler/tests/fixture_entry_mid_insn.py [--recomp <recomp.py>]
"""
import json, os, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NATIV = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
RECOMP = sys.argv[sys.argv.index("--recomp") + 1] if "--recomp" in sys.argv else os.path.join(HERE, os.pardir, "recomp.py")
t = lambda *h: b"".join(struct.pack("<H", x) for x in h)
F = 0x81000000
E = F + 4
code = t(0x2005, 0xF04F, 0x0200, 0x3001, 0x4770)
d = {"blocks": [{"start": F, "size": len(code), "init": True, "bytes": code.hex()}],
     "functions": {"FUN_%08x" % F: F, "FUN_%08x" % E: E},
     "bodies": [[F, F + 9, F], [F, F + 5, E], [E, F + 9, E]], "relocs": [], "module": "fixture", "exports": [],
     "variables": [], "arm": []}
results = []
def check(name, ok, detail=""):
    print(("PASS  " if ok else "FAIL  ") + name + (("  -- " + detail) if detail and not ok else ""))
    results.append(ok)


work = tempfile.mkdtemp(prefix="fixture_emi_")
js = os.path.join(work, "m.data.json"); json.dump(d, open(js, "w"))
r = subprocess.run([sys.executable, RECOMP, js, os.path.join(work, "m.c"), "--split", "1"], capture_output=True, text=True)
if r.returncode: print("recomp failed:", (r.stdout + r.stderr)[-600:]); sys.exit(2)
src = "".join(open(os.path.join(work, x)).read() for x in os.listdir(work) if x.endswith(".c"))
check("fn_%08x has the label it jumps to" % E, ("L_%08x:" % E) in src)
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
        f.write("int main(void) {\n\trecomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);\n")
        f.write("\t{ static const uint8_t b[] = {%s}; memcpy(recomp_mem + 0x%xu, b, sizeof b); }\n" % (",".join(str(x) for x in code), F))
        for name, fn in (("F", F), ("E", E)):
            f.write("\t{ static Cpu c; memset(&c, 0, sizeof c); c.r[13] = 0x100000; c.r[0] = 1; c.r[2] = 77; fn_%08x(&c); printf(\"%s %%u %%u\\n\", c.r[0], c.r[2]); }\n" % (fn, name))
        f.write("\treturn 0;\n}\n")
    exe = os.path.join(work, "run")
    srcs = [os.path.join(work, x) for x in sorted(os.listdir(work)) if x.endswith(".c")]
    r = subprocess.run([cc, "-O2", "-w", "-I", os.path.join(NATIV, "recompiler"), "-o", exe] + srcs + ["-lm"], capture_output=True, text=True)
    if r.returncode:
        check("generated C compiles", False, r.stderr[-800:])
    else:
        check("generated C compiles", True)
        rows = {l.split()[0]: (int(l.split()[1]), int(l.split()[2])) for l in subprocess.run([exe], capture_output=True, text=True).stdout.splitlines() if l.strip()}
        check("fn_F: movs #5, mov.w r2 #0, adds #1 -> r0 6 r2 0", rows.get("F") == (6, 0), str(rows.get("F")))
        check("fn_E: lsls #8, adds #1 -> r0 257, r2 untouched (77)", rows.get("E") == (257, 77), str(rows.get("E")))
shutil.rmtree(work, ignore_errors=True)
failed = results.count(False)
print(f"\n{len(results) - failed}/{len(results)} checks passed")
sys.exit(0 if failed == 0 else 1)
