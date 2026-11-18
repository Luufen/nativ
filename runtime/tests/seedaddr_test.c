/* seedaddr_test.c -- which missed call is recorded as a seed (runtime/core/seedaddr.h, see its header comment).
 * Fail-before: -DSEEDADDR_BEFORE is the old rule (0x81000000..0xa0000000 only).
 *
 * Run (WSL, from this folder):
 *   cc -O1 -Wall -I ../core seedaddr_test.c -o /tmp/seedaddr_test && /tmp/seedaddr_test
 *   cc -O1 -Wall -I ../core -DSEEDADDR_BEFORE seedaddr_test.c -o /tmp/seedaddr_before && /tmp/seedaddr_before
 */
#include <stdio.h>
#include "seedaddr.h"
#ifdef SEEDADDR_BEFORE
#define seed_addr(a, lo, hi, n) ((a) >= 0x81000000u && (a) < 0xa0000000u)
#define seed_image(a, lo, hi, n) (-1)   /* (no image lookup before GAPS-02) */
#define seed_thumb(a, lr, before, r) (((a) & 3) || ((lr) & 1))   /* (the lr rule alone, before PSN-NOVA) */
#endif

static int fails;
static void check(const char *what, int ok) { printf("%-72s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }

int main(void) {
	/* synthetic images: the main program, a module in the first slots, one at slot 0x3d (0xc5000000) and its slots */
	const uint32_t lo[] = {0x81000000u, 0x88000000u, 0xc5000000u, 0xc5001000u, 0xffe25800u};
	const uint32_t hi[] = {0x81465000u, 0x88052000u, 0xc50005b0u, 0xc5001100u, 0xffe26800u};
	const int n = 5;
	check("main program code is a seed", seed_addr(0x81044f3du, lo, hi, n));
	check("a module in the first slots is a seed", seed_addr(0x88001235u, lo, hi, n));
	check("a module's code at 0xc5000000 (past 0xa0000000) is a seed", seed_addr(0xc50002f7u, lo, hi, n));
	check("  ARM (even) address there too", seed_addr(0xc5000300u, lo, hi, n));
	check("past that module's end is not", !seed_addr(0xc50005b0u, lo, hi, n));
	check("a null call is not", !seed_addr(0, lo, hi, n));
	check("a poisoned pointer (0xdeadbeef, no image) is not", !seed_addr(0xdeadbeefu, lo, hi, n));
	check("an imported-variable slot is not", !seed_addr(0xffe25801u, lo, hi, n));
	check("GAPS-02: a missed function's image: the segment it lies in (0xc50002f7 -> 2)", seed_image(0xc50002f7u, lo, hi, n) == 2);
	check("GAPS-02: main program code -> segment 0; a poisoned pointer -> none (-1)", seed_image(0x81044f3du, lo, hi, n) == 0 && seed_image(0xdeadbeefu, lo, hi, n) == -1);
	/* PSN-NOVA: the instruction set of a missed function (seed_thumb). Synthetic registers and encodings */
	uint32_t r[16] = {0};
	r[2] = 0x81100890u;   /* an even word, as a vtable slot holding an ARM function */
	check("Thumb blx r2 (0x4790) to an even target: ARM", seed_thumb(0x81100890u, 0x81000683u, 0x4790u, r) == 0);
	r[2] = 0x81100891u;
	check("Thumb blx r2 to an odd target: Thumb", seed_thumb(0x81100891u, 0x81000683u, 0x4790u, r) == 1);
	r[3] = 0x81100894u;
	check("ARM blx r3 (0xe12fff33) to an even target: ARM", seed_thumb(0x81100894u, 0x81000684u, 0xe12fff33u, r) == 0);
	r[3] = 0x81100895u;
	check("ARM blx r3 to an odd target: Thumb", seed_thumb(0x81100895u, 0x81000684u, 0xe12fff33u, r) == 1);
	check("Thumb bl (no BLX before lr) to an even target: Thumb, as before", seed_thumb(0x81100898u, 0x81000687u, 0xf800u, r) == 1);
	check("ARM bl to a 4-aligned target: ARM, as before", seed_thumb(0x811008a0u, 0x81000688u, 0xeb000010u, r) == 0);
	check("ARM caller, 2-aligned target: Thumb, as before", seed_thumb(0x811008a2u, 0x81000688u, 0xeb000010u, r) == 1);
	r[2] = 0x81222222u;   /* a stale lr: the BLX before it called something else (a tail jump since): the lr rule */
	check("stale lr (blx r2 before it, r2 no longer the target): Thumb by lr, as before", seed_thumb(0x81100890u, 0x81000683u, 0x4790u, r) == 1);
	if (fails) { printf("%d checks FAILED\n", fails); return 1; }
	printf("all passed\n");
	return 0;
}
