/* target_rows_test.c -- a partial download of a render target for a small texture inside it (runtime/gfx/target_rows.h).
 * A model target (guest memory + the GPU's newer pixels) is read by textures at many places and sizes: the download
 * the rule picks (its rows, else the whole target) must hand the texture every byte it reads as the GPU has it, and for
 * a texture of at most a quarter of the target bring down at most the rows its bytes touch (the LittleBigPlanet case: a
 * 32x32 U8 texture, 1 KB, at row 95 of a 768x1024 U8U8U8U8 target). Rows outside stay as guest memory had them (the
 * target stays GPU-newer). -DTARGET_ROWS_UPSTREAM_RULE builds the rule before it -- always the whole target -- as the
 * control, which fails the size checks. Pure logic:
 *   cc -O1 -g -Wall -I../gfx target_rows_test.c -o /tmp/target_rows_test && /tmp/target_rows_test */
#include <stdint.h>
#ifdef TARGET_ROWS_UPSTREAM_RULE
static int target_rows_for(uint32_t addr, uint32_t row_bytes, uint32_t height, uint32_t lo, uint32_t hi, uint32_t *y0, uint32_t *y1) { (void)addr; (void)row_bytes; (void)height; (void)lo; (void)hi; (void)y0; (void)y1; return 0; }
#else
#include "target_rows.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failed;
#define CK(c) do { checks++; if (!(c)) { failed++; if (failed < 20) printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

enum { W = 768, H = 1024, RB = 4 * W };
static uint8_t gpu_px[RB * H], guest[RB * H];

/* one read of bytes [off, off + len) of a target at `addr`: download as the rule says, then check */
static void one(uint32_t addr, uint32_t off, uint32_t len) {
	for (uint32_t i = 0; i < RB * H; i++) { gpu_px[i] = (uint8_t)(i * 7 + 3); guest[i] = (uint8_t)(i * 13 + 1); }
	uint32_t y0, y1, down_rows;
	if (target_rows_for(addr, RB, H, addr + off, addr + off + len, &y0, &y1)) {
		CK(y0 < y1 && y1 <= H);
		memcpy(guest + y0 * RB, gpu_px + y0 * RB, (size_t)(y1 - y0) * RB);
		down_rows = y1 - y0;
		/* rows outside the download keep guest memory's bytes */
		int kept = 1; for (uint32_t y = 0; y < H && kept; y++) if (y < y0 || y >= y1) kept = memcmp(guest + y * RB, gpu_px + y * RB, RB) != 0;
		CK(kept);
	} else { memcpy(guest, gpu_px, sizeof guest); down_rows = H; }
	/* the texture reads the GPU's bytes */
	CK(memcmp(guest + off, gpu_px + off, len) == 0);
	/* a small texture brings down only the rows its bytes touch */
	const uint32_t touched = (off + len + RB - 1) / RB - off / RB;
	if (touched * 4 <= H) CK(down_rows == touched);
}

int main(void)
{
	const uint32_t addr = 0x204dc880u;
	one(addr, 0x20524300u - addr, 32 * 32);     /* LittleBigPlanet: 32x32 U8 Morton texture, row 95 */
	one(addr, 0, 1);                             /* first byte */
	one(addr, RB * H - 1, 1);                    /* last byte */
	one(addr, RB - 2, 4);                        /* across a row boundary: two rows */
	one(addr, RB * 10, RB * (H / 4));            /* exactly a quarter: still partial */
	one(addr, RB * 10, RB * (H / 4) + 1);        /* just over a quarter: the whole target */
	one(addr, 0, RB * H);                        /* the whole target */
	for (uint32_t k = 0; k < 400; k++) {        /* many places and sizes */
		uint32_t off = (k * 2654435761u) % (RB * H), len = 1 + (k * 40503u) % (RB * 64);
		if (off + len > RB * H) len = RB * H - off;
		one(addr, off, len);
	}
	/* no overlap, empty and bad arguments: no partial download */
	uint32_t y0, y1;
	CK(!target_rows_for(addr, RB, H, addr - 100, addr, &y0, &y1));
	CK(!target_rows_for(addr, RB, H, addr + RB * H, addr + RB * H + 10, &y0, &y1));
	CK(!target_rows_for(addr, RB, H, addr + 50, addr + 50, &y0, &y1));
	CK(!target_rows_for(addr, 0, H, addr + 50, addr + 60, &y0, &y1));
	/* a texture that starts before the target: rows from 0 */
	CK(target_rows_for(addr, RB, H, addr - 64, addr + 64, &y0, &y1) && y0 == 0 && y1 == 1);
	/* near the top of the 32-bit space: no overflow */
	CK(target_rows_for(0xfff00000u, 256, 4096, 0xfff00100u, 0xfff00200u, &y0, &y1) && y0 == 1 && y1 == 2);
	printf("%d checks, %d failed\n", checks, failed);
	return failed != 0;
}
