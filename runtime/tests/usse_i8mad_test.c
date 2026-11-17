/*
 * Synthetic USSE I8MAD fixture: the four U8 lanes are one packed word in and out (docs/usse-isa.md §3.12).
 *
 * I8MAD computes, per lane, m1 * m2 (as U8 fractions) plus or minus an addend chosen by csel0 / asel0, with csel1 /
 * csel2 replacing a factor's rgb lanes by its alpha. The decoder types its operands TY_U8, and the interpreter used to
 * read and write them with that type: read_i32 returned lane 0 alone and write_raw_lane stored lane 0 alone, so green,
 * blue and alpha came out 0 -- and src0's negate bit, which only selects subtract, negated the read word too.
 * (Found and fixed in Tzeentchnet/nativ 5796a7e for Resistance: Burning Skies' shadow resolve; of our games, Tearaway
 * uses I8MAD in 40 shader programs, Freedom Wars and Killzone Mercenary in 2 each.)
 *
 * The fixture hand-encodes I8MAD words from the ISA table, decodes them with the real decoder, runs the interpreter
 * against an oracle computed here from the packed words, and hands the program to the SPIR-V translator (it must
 * accept it). Fail-before: build it against the pre-fix usse.c -- the g, b, a lanes come out 0.
 * Build and run: see README.md in this directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "usse.h"
#include "usse_internal.h"
#include "usse_spirv.h"

uint8_t *recomp_mem;
uint32_t recomp_watch_lo, recomp_watch_hi;
void recomp_watch_write(uint32_t a, uint32_t v, int width) { (void)a; (void)v; (void)width; }

static int checks, failed;
static void ck(int cond, const char *what, unsigned long long a, unsigned long long b) {
	checks++;
	if (!cond) { failed++; printf("  FAIL %s: got %#llx want %#llx\n", what, a, b); }
}

#define GXP_ADDR 0x10000000u
#define CODE_OFF 128
static void put32(uint32_t addr, uint32_t v) { memcpy(recomp_mem + addr, &v, 4); }
static void put64(uint32_t addr, uint64_t v) { memcpy(recomp_mem + addr, &v, 8); }
static void build_gxp(uint64_t insn) {   /* usse.c's header layout, as usse_sop2_test.c */
	memset(recomp_mem + GXP_ADDR, 0, 256);
	put32(GXP_ADDR + 8, 256); put32(GXP_ADDR + 20, 1); put32(GXP_ADDR + 48, 16);
	put32(GXP_ADDR + 60, 1); put32(GXP_ADDR + 64, CODE_OFF - 64);
	put64(GXP_ADDR + CODE_OFF, insn);
}

/* I8MAD, fields per docs/usse-isa.md §3.12 (bit 63 first); all banks TEMP, no modifiers, end set */
static uint64_t i8mad_word(int csel0, int asel0, int csel1, int csel2, int neg, int count, int dst, int s0, int s1, int s2) {
	uint64_t w = 0x13ull << 59;
	w |= (uint64_t)(csel0 & 3) << 52;
	w |= 1ull << 50;                         /* end */
	w |= (uint64_t)(count & 7) << 44;
	w |= (uint64_t)(asel0 & 1) << 41;
	w |= (uint64_t)(csel1 & 1) << 37;
	w |= (uint64_t)(csel2 & 1) << 36;
	w |= (uint64_t)(neg & 1) << 35;
	w |= (uint64_t)(dst & 0x7F) << 21 | (uint64_t)(s0 & 0x7F) << 14 | (uint64_t)(s1 & 0x7F) << 7 | (uint64_t)(s2 & 0x7F);
	return w;
}

/* the oracle: the packed words in, per lane (m1 * m2) / 255 +- the addend, clamped to 0..255 */
static uint32_t expect(uint32_t w0, uint32_t w1, uint32_t w2, int csel0, int asel0, int csel1, int csel2, int neg) {
	uint32_t out = 0;
	for (int k = 0; k < 4; k++) {
		const int a1 = (int)(((csel1 && k < 3) ? w1 >> 24 : w1 >> (8 * k)) & 0xFF);
		const int a2 = (int)(((csel2 && k < 3) ? w2 >> 24 : w2 >> (8 * k)) & 0xFF);
		int add;
		if (k < 3) add = (int)((csel0 == 0 ? w0 >> (8 * k) : csel0 == 1 ? w1 >> (8 * k) : csel0 == 2 ? w0 >> 24 : w1 >> 24) & 0xFF);
		else add = (int)((asel0 ? w1 : w0) >> 24);
		int v = a1 * a2 / 255; v = neg ? v - add : v + add;
		if (v < 0) v = 0; if (v > 255) v = 255;
		out |= (uint32_t)v << (8 * k);
	}
	return out;
}

struct Case { const char *name; int csel0, asel0, csel1, csel2, neg; uint32_t w0, w1, w2; };

int main(void) {
	recomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (recomp_mem == MAP_FAILED) { perror("mmap"); return 2; }
	static const struct Case cases[] = {
		/* name                                   csel0 asel0 csel1 csel2 neg  w0 (add)    w1          w2 */
		{"s1*s2 + s0, every lane",                  0,    0,    0,    0,   0, 0x10203040, 0x80FF4020, 0xFF80FF40},
		{"s1*s2 - s0 (src0_neg selects subtract)",   0,    0,    0,    0,   1, 0x08101820, 0xFFFFFFFF, 0x80C0E0F0},
		{"add s1.rgb / s1.a, alpha factors",         1,    1,    1,    1,   0, 0x00000000, 0x40302010, 0x80706050},
		{"add s0.aaa, s2 alpha into rgb",            2,    0,    0,    1,   0, 0x60000000, 0x10203040, 0xC0000000},
		{"a lit byte merged into the green lane",    0,    0,    0,    0,   0, 0x00000000, 0x0000FF00, 0x0000FF00},
	};
	for (unsigned ci = 0; ci < sizeof cases / sizeof cases[0]; ci++) {
		const struct Case *c = &cases[ci];
		build_gxp(i8mad_word(c->csel0, c->asel0, c->csel1, c->csel2, c->neg, 0, 4, 8, 12, 16));
		UsseProgram *p = usse_decode_uncached(GXP_ADDR);
		int n = 0; const UOp *code = usse_program_code(p, 0, &n);
		char what[160];
		snprintf(what, sizeof what, "[%s] decodes as one I8MAD", c->name);
		ck(n == 1 && code && code[0].op == OP_I8MAD, what, code ? code[0].op : 99, OP_I8MAD);

		UsseInst inst; memset(&inst, 0, sizeof inst);
		UsseDraw draw; memset(&draw, 0, sizeof draw);
		inst.r[8].u = c->w0; inst.r[12].u = c->w1; inst.r[16].u = c->w2;
		usse_run_primary(p, &inst, &draw, 0, 0);
		const uint32_t want = expect(c->w0, c->w1, c->w2, c->csel0, c->asel0, c->csel1, c->csel2, c->neg);
		snprintf(what, sizeof what, "[%s] interpreter: the packed word", c->name);
		ck(inst.r[4].u == want, what, inst.r[4].u, want);
		usse_program_free(p);

		p = usse_decode_uncached(GXP_ADDR);
		usse_spirv_set_canonical_varyings(1);
		uint32_t sa[256] = {0}; int units[16] = {0}, nsmp = 0;
		SpirvModule m = usse_spirv_translate(p, 0, usse_fragment_layout(p), sa, 16, units, &nsmp, 0, 0, 0, 0);
		snprintf(what, sizeof what, "[%s] the SPIR-V translator takes it", c->name);
		ck(m.ok && m.nwords > 0, what, (unsigned long long)m.ok, 1);
		if (!m.ok) printf("    err: %s\n", m.err);
		usse_spirv_free(&m);
		usse_program_free(p);
	}
	printf("%d checks, %d failed\n", checks, failed);
	return failed ? 1 : 0;
}
