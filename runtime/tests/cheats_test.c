/* cheats_test.c -- VitaCheat cheat files on a buffer standing in for guest memory (CHEAT-01, runtime/core/cheats.h).
 *
 * The contract: a .psv's cheats (_V0 off, _V1 on, comments/headers/Seg lines skipped, a BOM and CRLF read), and every
 * code type VitaCheat's wiki documents applied as written there: write, copy, compression, pointer write (levels 1-3),
 * pointer compression, pointer copy, data write (restored when switched off; refused into code), buttons, conditions
 * (=, <>, >, < at 8/16/32 bits), B2 module-relative addresses. A write outside the game's memory is skipped, a pointer
 * not set up yet writes nothing, an unknown code type marks the cheat unsupported.
 * Fail-before: cheats.h did not exist (does not compile).
 *
 * Run (WSL, from this folder):
 *   cc -O1 -g -Wall -I ../core cheats_test.c -o /tmp/cheats_test && /tmp/cheats_test
 */
#include <stdio.h>
#include <string.h>
#include "cheats.h"

#define BASE 0x81000000u
#define SIZE 0x10000u
static uint8_t mem[SIZE];
static int fails, checks;
static void check(const char *what, int ok) { checks++; printf("%-74s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }

static int m_ok(void *c, uint32_t a, uint32_t n) { (void)c; return a >= BASE && a - BASE + n <= SIZE && a - BASE + n >= n; }
static int m_code(void *c, uint32_t a) { (void)c; return a >= BASE && a < BASE + 0x1000; }   /* (the first 4 KiB: "code") */
static uint32_t m_rd(void *c, uint32_t a, int b) { (void)c; uint32_t v = 0; memcpy(&v, mem + (a - BASE), b); return v; }
static void m_wr(void *c, uint32_t a, int b, uint32_t v) { (void)c; memcpy(mem + (a - BASE), &v, b); }
static int m_seg(void *c, int mod, int seg, uint32_t *base) { (void)c; if (mod != 0 || seg > 1) return 0; *base = seg ? BASE + 0x8000 : BASE; return 1; }
static CheatMem M = {0, m_ok, m_code, m_rd, m_wr, m_seg, 0};
static uint32_t r32(uint32_t a) { return m_rd(0, a, 4); }
static void w32(uint32_t a, uint32_t v) { m_wr(0, a, 4, v); }
static int find(CheatSet *s, const char *name) { for (int i = 0; i < s->nc; i++) if (!strcmp(s->c[i].name, name)) return i; return -1; }
static int run(CheatSet *s, const char *name) { int i = find(s, name); return i < 0 ? -2 : cheat_apply(s, i, &M, 1); }

static const char *file =
	"\xEF\xBB\xBF# PCSA00147 Freedom Wars\r\n"
	"# Version: 1.00\r\n"
	"\r\n"
	"_V0 Write 8 16 32\r\n"
	"$0000 81002000 000000AB\r\n"
	"$0100 81002002 0000BEEF\r\n"
	"$0200 81002004 3B9AC9FF #a trailing comment\r\n"
	"_V1 Copy\r\n"
	"$5200 81002010 81002004\r\n"
	"_V0 Compression\r\n"
	"$4201 81002100 00000063\r\n"
	"$0004 00000004 00000001\r\n"
	"_V0 Pointer 1\r\n"
	"$3201 81003000 00000010\r\n"
	"$3300 00000000 11111111\r\n"
	"_V0 Pointer 3\r\n"
	"$3203 81003004 00000008\r\n"
	"$3200 00000000 0000000C\r\n"
	"$3200 00000000 00000004\r\n"
	"$3300 00000000 33333333\r\n"
	"_V0 Pointer compression\r\n"
	"$7201 81003000 00000020\r\n"
	"$7701 00000000 00000005\r\n"
	"$0003 00000004 00000002\r\n"
	"_V0 Pointer copy\r\n"
	"$8201 81003000 00000040\r\n"
	"$8800 00000000 00000000\r\n"
	"$8601 81003000 00000044\r\n"
	"$8900 00000000 00000000\r\n"
	"_V0 Data write\r\n"
	"$A200 81002200 CAFEBABE\r\n"
	"_V0 Code write\r\n"
	"$A100 81000100 0000BF00\r\n"
	"_V0 Buttons L+R\r\n"
	"$C201 00000001 00000300\r\n"
	"$0200 81002300 00000001\r\n"
	"$0200 81002304 00000002\r\n"
	"_V0 Condition 16 equal\r\n"
	"$D101 81002400 00001234\r\n"
	"$0200 81002404 00000077\r\n"
	"_V0 Condition 32 less\r\n"
	"$DB01 81002408 00000010\r\n"
	"$0200 8100240C 00000088\r\n"
	"_V0 B2 data segment\r\n"
	"$B200 00000000 00000001\r\n"
	"$0200 00000500 000000B2\r\n"
	"_V0 B2 unknown module\r\n"
	"$B203 00000000 00000000\r\n"
	"$0200 00000500 000000B2\r\n"
	"Seg0:81000000-811F9188\r\n"
	"_V0 Outside memory\r\n"
	"$0200 90000000 00000001\r\n"
	"$0200 81002500 00000005\r\n"
	"_V0 Unknown type\r\n"
	"$E200 81002600 00000001\r\n";

int main(void) {
	CheatSet s = {0};
	check("the file reads: 16 cheats", cheats_parse(file, &s) == 16);
	check("names without their identifier, a comment line skipped", find(&s, "Write 8 16 32") == 0 && find(&s, "Copy") == 1);
	check("_V0 off, _V1 on", !s.c[0].def_on && s.c[1].def_on);
	check("a trailing '#' comment doesn't break the line", s.l[s.c[0].first + 2].b == 0x3B9AC9FF);
	check("a Seg0: note is not a code line", s.c[find(&s, "B2 unknown module")].n == 2);

	check("write: 8, 16 and 32 bits", run(&s, "Write 8 16 32") == 0 && mem[0x2000] == 0xAB && m_rd(0, 0x81002002, 2) == 0xBEEF && r32(0x81002004) == 0x3B9AC9FF);
	check("  the 8-bit write left its neighbour alone", mem[0x2001] == 0);
	check("copy: 32 bits from one address to another", run(&s, "Copy") == 0 && r32(0x81002010) == 0x3B9AC9FF);
	check("compression: 4 writes, address +4, value +1", run(&s, "Compression") == 0 && r32(0x81002100) == 0x63 && r32(0x81002104) == 0x64 && r32(0x8100210C) == 0x66 && r32(0x81002110) == 0);

	check("pointer 1, the pointer not set up yet: nothing written", run(&s, "Pointer 1") == 0 && r32(0x81004010) == 0);
	w32(0x81003000, 0x81004000);
	check("pointer 1: [base] + offset", run(&s, "Pointer 1") == 0 && r32(0x81004010) == 0x11111111);
	w32(0x81003004, 0x81005000); w32(0x81005008, 0x81006000); w32(0x8100600C, 0x81007000);
	check("pointer 3: three links followed", run(&s, "Pointer 3") == 0 && r32(0x81007004) == 0x33333333);
	check("pointer compression: 3 writes from the last offset, value +2", run(&s, "Pointer compression") == 0 &&
	      r32(0x81004020) == 5 && r32(0x81004024) == 7 && r32(0x81004028) == 9);
	w32(0x81004044, 0x5A5A5A5A);
	check("pointer copy: one chain's value into the other's", run(&s, "Pointer copy") == 0 && r32(0x81004040) == 0x5A5A5A5A);

	w32(0x81002200, 0x12345678);
	int dw = find(&s, "Data write");
	check("data write: written", cheat_apply(&s, dw, &M, 1) == 0 && r32(0x81002200) == 0xCAFEBABE);
	check("  applied again, still its value", cheat_apply(&s, dw, &M, 0) == 0 && r32(0x81002200) == 0xCAFEBABE);
	cheat_restore(&s, dw, &M);
	check("  switched off: the value before it is back", r32(0x81002200) == 0x12345678);
	m_wr(0, 0x81000100, 2, 0x4770);
	check("code write: refused, unsupported", run(&s, "Code write") == -1 && m_rd(0, 0x81000100, 2) == 0x4770);
	check("  its note says why", strstr(s.c[find(&s, "Code write")].note, "code") != NULL);

	M.pad = 0x100;
	check("buttons: L alone -> its one related line skipped, the line after it runs", run(&s, "Buttons L+R") == 0 && r32(0x81002300) == 0 && r32(0x81002304) == 2);
	M.pad = 0x300 | 0x4000;
	check("buttons: L+R held (and more) -> its related line runs", run(&s, "Buttons L+R") == 0 && r32(0x81002300) == 1 && r32(0x81002304) == 2);
	M.pad = 0;
	check("condition =, 16-bit, not met: skipped", run(&s, "Condition 16 equal") == 0 && r32(0x81002404) == 0);
	m_wr(0, 0x81002400, 2, 0x1234);
	check("condition =, 16-bit, met: its line runs", run(&s, "Condition 16 equal") == 0 && r32(0x81002404) == 0x77);
	w32(0x81002408, 0x20);
	check("condition <, 32-bit, not met (0x20 < 0x10)", run(&s, "Condition 32 less") == 0 && r32(0x8100240C) == 0);
	w32(0x81002408, 0x0F);
	check("condition <, 32-bit, met", run(&s, "Condition 32 less") == 0 && r32(0x8100240C) == 0x88);

	check("B2: addresses relative to module 0's data segment", run(&s, "B2 data segment") == 0 && r32(BASE + 0x8000 + 0x500) == 0xB2);
	check("B2 of a module the game hasn't: unsupported", run(&s, "B2 unknown module") == -1);
	check("a write outside the game's memory is skipped, the next one runs", run(&s, "Outside memory") == 0 && r32(0x81002500) == 5);
	check("an unknown code type: unsupported, nothing written", run(&s, "Unknown type") == -1 && r32(0x81002600) == 0);

	CheatSet t = {0};
	check("an empty file: no cheats", cheats_parse("# nothing\n\n", &t) == 0);
	check("code lines before any cheat are ignored", cheats_parse("$0200 81000000 00000001\n_V1 A\n$0200 81000004 00000002\n", &t) == 1 && t.c[0].n == 1);
	cheats_free(&t); cheats_free(&s);
	printf(fails ? "%d of %d checks FAILED\n" : "%d checks, all passed\n", fails ? fails : checks, checks);
	return fails != 0;
}
