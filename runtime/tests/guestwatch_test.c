/* guestwatch_test: the read watch and gwatch_keep_* (guestwatch.c) on a real guest arena with the real fault handler.
 *
 * The case (Phantasy Star Nova, NOVA-14): a GPU render target is left on the GPU after its scene, its pages read-armed;
 * the game's CPU then stores new data (textures) into that memory; later the target comes down to guest memory (a host
 * write of the whole range). The bytes the CPU stored after the arm must survive that download -- gwatch_keep_begin
 * before it, gwatch_keep_end after -- and the other pages must take the download.
 *
 *   cc -O1 -g -Wall -I ../gfx -I ../platform -I ../core -I ../../recompiler guestwatch_test.c ../gfx/guestwatch.c \
 *      ../platform/platform_posix.c -o /tmp/guestwatch_test -lpthread -ldl && /tmp/guestwatch_test
 *   (-DNO_KEEP: the download without gwatch_keep_*, as target_download_to_guest did before: the store is lost) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt.h"
#include "guestwatch.h"
#include "platform.h"

uint8_t *recomp_mem;
static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
static int hook_calls; static uint32_t hook_addr;
static void hook(uint32_t a) { hook_calls++; hook_addr = a; }

int main(void) {
	recomp_mem = plat_reserve(1ull << 32);   /* the 4 GB guest arena, as main.c's */
	if (!recomp_mem) { fprintf(stderr, "no arena\n"); return 1; }
	if (!gwatch_on()) { fprintf(stderr, "guest-memory tracking unavailable here\n"); return 1; }
	const uint32_t ps = 1u << gwatch_page_shift(), base = 0x204c0000u, len = 16 * ps;   /* a target of 16 pages */
	gwatch_set_read_hook(hook);

	/* the target's scene ends: its pixels are what the GPU drew (here: 0x11 in guest memory, older), its pages armed */
	memset(G(base), 0x11, len);
	const uint32_t since = gwatch_protect(0, 0);
	gwatch_read_arm(base, len);
	/* the game's CPU stores into page 5 and page 9 (a texture load), as guest code does: a plain store, which faults */
	memset(G(base + 5 * ps + 64), 0xb1, 256);
	*(volatile uint32_t *)G(base + 9 * ps) = 0xbc1bc1bcu;
	CHECK(hook_calls == 2);
	CHECK(hook_addr >= base + 9 * ps && hook_addr < base + 10 * ps);

	/* the target comes down to guest memory: the GPU's pixels (0x22) over the whole range */
#ifndef NO_KEEP
	GwatchKeep *k = gwatch_keep_begin(base, len, since);
#endif
	gwatch_write_begin(base, len); memset(G(base), 0x22, len); gwatch_write_end(base, len);
	uint32_t kept = 0;
#ifndef NO_KEEP
	kept = gwatch_keep_end(k);
#endif
	CHECK(kept == 2);
	CHECK(G(base + 5 * ps + 64)[0] == 0xb1 && G(base + 5 * ps + 64)[255] == 0xb1);   /* the CPU's store survived */
	CHECK(*(uint32_t *)G(base + 9 * ps) == 0xbc1bc1bcu);
	CHECK(G(base + 5 * ps)[0] == 0x11);   /* the rest of a kept page: what guest memory held (the CPU's view), not the download */
	CHECK(G(base)[0] == 0x22 && G(base + 15 * ps + ps - 1)[0] == 0x22 && G(base + 6 * ps)[0] == 0x22);   /* untouched pages: the download */

	/* nothing stamped after a newer clock: nothing kept, and an unstamped armed page is not touched by the scan */
	const uint32_t since2 = gwatch_protect(0, 0);
	gwatch_read_arm(base, len);
	const int calls_before = hook_calls;
	GwatchKeep *k2 = gwatch_keep_begin(base, len, since2);
	CHECK(k2 == 0);
	CHECK(gwatch_keep_end(k2) == 0);
	CHECK(hook_calls == calls_before);   /* the scan read no armed page */
	gwatch_read_open(base, len);
	CHECK(G(base + 3 * ps)[0] == 0x22);

	fprintf(stderr, "%d checks, %d failed\n", checks, fails);
	if (!fails) fprintf(stderr, "all passed\n");
	return fails ? 1 : 0;
}
