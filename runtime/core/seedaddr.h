/* seedaddr.h -- which call to code that is neither recompiled nor an import is a missed function (main.c
 * recomp_call_external records those for analyze.py --seeds; runtime/tests/seedaddr_test.c).
 *
 * A missed function lives in a loaded image: the main program (0x81000000 up) or a module, which the pipeline places
 * at 0x88000000 + 16 MB each (vitarecomp.py mod_base), so a game with many on-demand modules (LOAD_MODULES) has
 * code well past 0xa0000000. Before: only 0x81000000..0xa0000000 counted, so a module's missed function up there
 * stopped the run without a seed and bootstrap could not get past it (NIGHT-01 batch B, a game module at 0xc5000000).
 * Not a missed function: a call through a null or poisoned pointer (outside every image), or into the
 * imported-variable slots (0xffe00000 up, data).
 */
#ifndef RECOMP_SEEDADDR_H
#define RECOMP_SEEDADDR_H
#include <stdint.h>

static inline int seed_addr(uint32_t addr, const uint32_t *lo, const uint32_t *hi, int n) {
	const uint32_t a = addr & ~1u;
	if (a >= 0x81000000u && a < 0xa0000000u) return 1;   /* the main program and the first modules (as before) */
	if (a >= 0xffe00000u) return 0;                      /* imported-variable slots */
	for (int i = 0; i < n; i++) if (a >= lo[i] && a < hi[i]) return 1;   /* a loaded image's segment */
	return 0;
}
/* a missed function's instruction set, 1 = Thumb, 0 = ARM. `before` is the instruction just before lr (the Thumb
 * halfword at lr - 3 for a Thumb caller, lr odd; the ARM word at lr - 4 otherwise), r the registers at the call.
 * A register BLX selects the state from the target's bit 0 (ARM ARM BLX (register), BXWritePC): Thumb code's
 * "blx r2" through a vtable slot holding an even word enters ARM code (PSN-NOVA, PCSH00143 0x8204c890, recorded as
 * Thumb because lr was odd: its Thumb decode broke r4 and the next virtual call jumped to junk). It counts only when
 * that BLX's register still holds the target, as on the call itself (lr can be stale: a tail jump keeps the caller's).
 * Otherwise as before: an odd address, one ARM code can't start at (not 4-aligned), or one Thumb code called or
 * branched to directly (a bl/b target carries no Thumb bit; np_matching2's 0x8cee was recorded as ARM) */
static inline int seed_thumb(uint32_t addr, uint32_t lr, uint32_t before, const uint32_t *r) {
	int m = -1;
	if (lr & 1) { if ((before & 0xff87u) == 0x4780u) m = (before >> 3) & 15; }      /* Thumb BLX Rm: 0100 0111 1 Rm 000 */
	else if ((before & 0x0ffffff0u) == 0x012fff30u) m = before & 15;               /* ARM BLX Rm: cond 0001 0010 1111 1111 1111 0011 Rm */
	if (m >= 0 && m != 14 && r[m] == addr) return addr & 1;
	return (addr & 3) || (lr & 1);
}
/* the loaded segment an address lies in (its index into lo/hi), -1 when none: names a missed function's image (GAPS) */
static inline int seed_image(uint32_t addr, const uint32_t *lo, const uint32_t *hi, int n) {
	const uint32_t a = addr & ~1u;
	for (int i = 0; i < n; i++) if (a >= lo[i] && a < hi[i]) return i;
	return -1;
}
#endif
