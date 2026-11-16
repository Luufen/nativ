/*
 * Cheats (CHEAT-01): VitaCheat's cheat files (.psv: the format of the VitaCheat plugin and its community databases,
 * r0ah/vitacheat wiki "Formatting" / "Code-Types"), applied to the guest's memory once per presented frame, as the
 * plugin does on a console. Nothing of a cheat database ships with Nativ: the player picks a file of their own.
 *
 *   # PCSA00147 Freedom Wars           (a comment; header lines, "Seg0:..." notes and empty lines are skipped)
 *   _V0 Max Gold                        a cheat, off until switched on (_V1: on from the start; _C0/_C1 read alike)
 *   $0200 81000000 3B9AC9FF             its code lines: $TXLL AAAAAAAA BBBBBBBB (T type, X width or operator, LL a count)
 *
 * Code types (X: 0 8-bit, 1 16-bit, 2 32-bit unless said):
 *   0 write          $0X00 addr value
 *   5 copy           $5X00 dst src
 *   4 compression    $4X01 addr value / $NNNN gap vgap: N writes, addr + k*gap <- value + k*vgap
 *   3 pointer write  $3X0L base off1 / (L-1) x $3X00 0 offK / $33.. 0 value: p = [base]; a = p + off1; deeper levels
 *                    p = [a], a = p + offK; then value -> a
 *   7 pointer compression  as 3 (value line $77LL 0 value), then $NNNN gap vgap: the last offset + k*gap <- value + k*vgap
 *   8 pointer copy   $8X0L base off1 / (L-1) x $8X00 / $8800 0 0 / $8Y0M src off1 / (M-1) x $8Y00 / $8900 0 0 (Y 4..6 = width)
 *   A data write     $AX00 addr value: restored when the cheat is switched off. Into the game's CODE it does nothing
 *                    here -- a recompiled game doesn't run its instructions from memory -- so such a cheat is marked
 *                    unsupported instead of written
 *   C buttons        $C2LL 0000000T buttons: the next LL lines only while those Vita buttons are held
 *   D condition      $DOLL addr value: the next LL lines only if [addr] <op> value (O: 0-2 =, 3-5 <>, 6-8 >, 9-B <;
 *                    8/16/32-bit in turn)
 *   B2 base          $B2MM 00000000 0000000S: the addresses after it relative to module MM's segment S (0 code, 1 data)
 * Anything else marks the cheat unsupported (its note says which), so a file never half-applies a code it can't read.
 *
 * Header-only C, no guest memory of its own: the caller passes a CheatMem (read, write, which addresses are the
 * game's memory, which are code, a module's segment base, the buttons held). runtime/core/cheats.c is the runtime's;
 * runtime/tests/cheats_test.c checks it against a buffer.
 */
#ifndef CHEATS_H
#define CHEATS_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHEAT_NAME_MAX 72
typedef struct { uint32_t code, a, b; uint32_t orig, at; uint8_t saved; } CheatLine;   /* (orig: an A line's value before it at `at`, kept to restore) */
typedef struct { char name[CHEAT_NAME_MAX]; int def_on, first, n; char note[128]; } Cheat;
typedef struct { Cheat *c; int nc; CheatLine *l; int nl; } CheatSet;
typedef struct {
	void *ctx;
	int (*ok)(void *ctx, uint32_t a, uint32_t n);                 /* [a, a+n) is the game's memory */
	int (*is_code)(void *ctx, uint32_t a);                         /* a lies in a module's code */
	uint32_t (*rd)(void *ctx, uint32_t a, int bytes);
	void (*wr)(void *ctx, uint32_t a, int bytes, uint32_t v);
	int (*seg)(void *ctx, int module, int seg, uint32_t *base);   /* a module's segment start; 0: none */
	uint32_t pad;                                                  /* the Vita buttons held (SCE_CTRL_* bits) */
} CheatMem;

static inline void cheats_free(CheatSet *s) { free(s->c); free(s->l); memset(s, 0, sizeof *s); }

/* the text of a .psv into s (replacing what it held): the number of cheats, -1 out of memory */
static inline int cheats_parse(const char *text, CheatSet *s) {
	cheats_free(s);
	int cap_c = 16, cap_l = 64;
	s->c = (Cheat *)calloc(cap_c, sizeof *s->c); s->l = (CheatLine *)calloc(cap_l, sizeof *s->l);
	if (!s->c || !s->l) { cheats_free(s); return -1; }
	for (const char *p = text; p && *p; ) {
		const char *e = strchr(p, '\n'); size_t n = e ? (size_t)(e - p) : strlen(p);
		char line[256]; if (n >= sizeof line) n = sizeof line - 1;
		memcpy(line, p, n); line[n] = 0; p = e ? e + 1 : NULL;
		char *q = line; while (*q == ' ' || *q == '\t' || (unsigned char)*q == 0xEF || (unsigned char)*q == 0xBB || (unsigned char)*q == 0xBF) q++;   /* (a UTF-8 BOM too) */
		size_t m = strlen(q); while (m && (q[m - 1] == '\r' || q[m - 1] == ' ' || q[m - 1] == '\t')) q[--m] = 0;
		if (q[0] == '_' && (q[1] == 'V' || q[1] == 'C') && (q[2] == '0' || q[2] == '1')) {
			if (s->nc == cap_c) { Cheat *c = (Cheat *)realloc(s->c, 2 * cap_c * sizeof *c); if (!c) { cheats_free(s); return -1; } memset(c + cap_c, 0, cap_c * sizeof *c); s->c = c; cap_c *= 2; }
			Cheat *c = &s->c[s->nc++];
			const char *nm = q + 3; while (*nm == ' ' || *nm == '\t') nm++;
			snprintf(c->name, sizeof c->name, "%s", *nm ? nm : "(no name)");
			c->def_on = q[2] == '1'; c->first = s->nl; c->n = 0; c->note[0] = 0;
		} else if (q[0] == '$' && s->nc) {
			unsigned code, a, b;
			if (sscanf(q + 1, "%x %x %x", &code, &a, &b) != 3) continue;
			if (s->nl == cap_l) { CheatLine *l = (CheatLine *)realloc(s->l, 2 * cap_l * sizeof *l); if (!l) { cheats_free(s); return -1; } s->l = l; cap_l *= 2; }
			CheatLine *l = &s->l[s->nl++]; memset(l, 0, sizeof *l);
			l->code = code & 0xffff; l->a = a; l->b = b;
			s->c[s->nc - 1].n++;
		}
	}
	return s->nc;
}

static inline int cheat_width(unsigned x) { return x == 0 ? 1 : x == 1 ? 2 : x == 2 ? 4 : 0; }

/* a pointer chain: [base] + off1, then [that] + offK; 0 when a link isn't the game's memory (not set up yet) */
static inline int cheat_chain(CheatMem *m, uint32_t base, const uint32_t *off, int levels, uint32_t *out) {
	if (!m->ok(m->ctx, base, 4)) return 0;
	uint32_t p = m->rd(m->ctx, base, 4), a = 0;
	for (int k = 0; k < levels; k++) {
		a = p + off[k];
		if (k + 1 < levels) { if (!m->ok(m->ctx, a, 4)) return 0; p = m->rd(m->ctx, a, 4); }
	}
	*out = a; return 1;
}

/* one cheat's lines on the memory, once (a frame's turn). first: its first turn since switched on (A lines keep the
 * value they replace). 0 applied (a pointer not set up yet counts: it's tried again next frame), -1 unsupported (note set) */
static inline int cheat_apply(CheatSet *s, int idx, CheatMem *m, int first) {
	Cheat *c = &s->c[idx]; CheatLine *L = s->l + c->first; const int n = c->n;
	uint32_t base = 0;
#define CHEAT_BAD(...) do { snprintf(c->note, sizeof c->note, __VA_ARGS__); return -1; } while (0)
	for (int i = 0; i < n; ) {
		const unsigned code = L[i].code, t = code >> 12 & 15, x = code >> 8 & 15, ll = code & 0xff;
		const uint32_t A = L[i].a, B = L[i].b;
		const int w = cheat_width(x);
		switch (t) {
		case 0x0:
			if (!w) CHEAT_BAD("width %u of a write isn't one VitaCheat has", x);
			if (m->ok(m->ctx, A + base, w)) m->wr(m->ctx, A + base, w, B);
			i++; break;
		case 0x5:
			if (!w) CHEAT_BAD("width %u of a copy isn't one VitaCheat has", x);
			if (m->ok(m->ctx, A + base, w) && m->ok(m->ctx, B + base, w)) m->wr(m->ctx, A + base, w, m->rd(m->ctx, B + base, w));
			i++; break;
		case 0x4: {
			if (!w || i + 1 >= n) CHEAT_BAD("a compression code without its second line");
			const uint32_t cnt = L[i + 1].code, gap = L[i + 1].a, vgap = L[i + 1].b;
			for (uint32_t k = 0; k < cnt; k++) { const uint32_t a = A + base + k * gap; if (m->ok(m->ctx, a, w)) m->wr(m->ctx, a, w, B + k * vgap); }
			i += 2; break; }
		case 0x3: case 0x7: {
			const int lv = (int)ll;
			if (!w || lv < 1 || lv > 16 || i + lv >= n) CHEAT_BAD("a pointer code with a level of %d and %d lines", lv, n - i);
			uint32_t off[16]; off[0] = B;
			for (int k = 1; k < lv; k++) off[k] = L[i + k].b;
			const uint32_t value = L[i + lv].b;
			if (t == 0x3) {
				uint32_t a; if (cheat_chain(m, A + base, off, lv, &a) && m->ok(m->ctx, a, w)) m->wr(m->ctx, a, w, value);
				i += lv + 1;
			} else {
				if (i + lv + 1 >= n) CHEAT_BAD("a pointer compression without its count line");
				const uint32_t cnt = L[i + lv + 1].code, gap = L[i + lv + 1].a, vgap = L[i + lv + 1].b, last = off[lv - 1];
				for (uint32_t k = 0; k < cnt; k++) {
					off[lv - 1] = last + k * gap;
					uint32_t a; if (cheat_chain(m, A + base, off, lv, &a) && m->ok(m->ctx, a, w)) m->wr(m->ctx, a, w, value + k * vgap);
				}
				i += lv + 2;
			}
			break; }
		case 0x8: {
			const int lv = (int)ll;
			if (!w || lv < 1 || lv > 16 || i + lv >= n) CHEAT_BAD("a pointer copy with a level of %d", lv);
			uint32_t off[16]; off[0] = B; for (int k = 1; k < lv; k++) off[k] = L[i + k].b;
			int j = i + lv + 1;   /* (past the $88 line) */
			if (j >= n || (L[j].code >> 12) != 0x8) CHEAT_BAD("a pointer copy without its source half");
			const unsigned sx = L[j].code >> 8 & 15; const int lv2 = (int)(L[j].code & 0xff);
			if (sx < 4 || sx > 6 || lv2 < 1 || lv2 > 16 || j + lv2 >= n) CHEAT_BAD("a pointer copy's source half isn't one VitaCheat writes");
			uint32_t off2[16]; off2[0] = L[j].b; for (int k = 1; k < lv2; k++) off2[k] = L[j + k].b;
			uint32_t d, sa;
			if (cheat_chain(m, A + base, off, lv, &d) && cheat_chain(m, L[j].a + base, off2, lv2, &sa) && m->ok(m->ctx, d, w) && m->ok(m->ctx, sa, w))
				m->wr(m->ctx, d, w, m->rd(m->ctx, sa, w));
			i = j + lv2 + 1; break; }
		case 0xA:
			if (!w) CHEAT_BAD("width %u of an ARM write isn't one VitaCheat has", x);
			if (m->is_code(m->ctx, A + base))
				CHEAT_BAD("changes the game's code at %#x: a recompiled game doesn't run its code from memory, so this has no effect", A + base);
			if (m->ok(m->ctx, A + base, w)) {
				if (first || !L[i].saved) { L[i].orig = m->rd(m->ctx, A + base, w); L[i].at = A + base; L[i].saved = 1; }
				m->wr(m->ctx, A + base, w, B);
			}
			i++; break;
		case 0xC:
			if (x != 2) CHEAT_BAD("a button code of width %u", x);
			i += (m->pad & B) == B && B ? 1 : (int)ll + 1; break;
		case 0xD: {
			if (x > 0xB) CHEAT_BAD("condition operator %X isn't one VitaCheat has", x);
			const int cw = x % 3 == 0 ? 1 : x % 3 == 1 ? 2 : 4; const unsigned op = x / 3;
			int yes = 0;
			if (m->ok(m->ctx, A + base, cw)) {
				const uint32_t v = m->rd(m->ctx, A + base, cw), want = cw == 4 ? B : B & ((1u << (8 * cw)) - 1);
				yes = op == 0 ? v == want : op == 1 ? v != want : op == 2 ? v > want : v < want;
			}
			i += yes ? 1 : (int)ll + 1; break; }
		case 0xB:
			if (x != 2) CHEAT_BAD("a $B%X code isn't one VitaCheat has", x);
			if (!m->seg(m->ctx, (int)ll, (int)(B & 15), &base)) CHEAT_BAD("its addresses are relative to module %u's segment %u, which this game hasn't", ll, B & 15);
			i++; break;
		default:
			CHEAT_BAD("code type %X isn't one Nativ reads (VitaCheat's 0 3 4 5 7 8 A B2 C D are)", t);
		}
	}
#undef CHEAT_BAD
	return 0;
}

/* a cheat switched off: its A lines' values put back */
static inline void cheat_restore(CheatSet *s, int idx, CheatMem *m) {
	Cheat *c = &s->c[idx];
	for (int i = 0; i < c->n; i++) {
		CheatLine *l = &s->l[c->first + i];
		const int w = cheat_width(l->code >> 8 & 15);
		if ((l->code >> 12) == 0xA && l->saved && w && m->ok(m->ctx, l->at, w)) m->wr(m->ctx, l->at, w, l->orig);
		l->saved = 0;
	}
}

#endif
