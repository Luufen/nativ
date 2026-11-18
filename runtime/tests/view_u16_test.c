/* view_u16_test.c -- a GPU view of a U16 render target read by a U16 texture (runtime/gfx/view_u16.h): for every
 * value the GPU target can hold (its RGBA8 colour texture: R = k/255) and every SWIZZLE1 texture swizzle, the view
 * (mode 6: R, placed by the selectors) must give what the texture decodes from the bytes a download stores. The
 * oracle restates gfx_soft.c independently: the U16 target store (to16), the U16 texel decode (word / 65535) and
 * apply_swizzle's SWIZZLE1 table. Other target/texture pairs stay refused (their own modes or the guest-memory path).
 * Before view_u16.h a U16 target had no view mode: every read downloaded the whole target (-DVIEW_U16_UPSTREAM_RULE
 * builds that rule instead: the control, which fails). Pure logic:
 *   cc -O1 -g -Wall -I../gfx view_u16_test.c -o /tmp/view_u16_test && /tmp/view_u16_test */
#include <stdint.h>
#ifdef VIEW_U16_UPSTREAM_RULE
#define VIEW_U16_MODE 6u
static int view_u16_mode(uint32_t tgt_fmt, uint32_t tex_fmt, uint32_t *tex_bpp) { (void)tgt_fmt; (void)tex_fmt; (void)tex_bpp; return -1; }   /* no U16 case in view_mode */
#else
#include "view_u16.h"
#endif
#include <math.h>
#include <stdio.h>

static int checks, failed;
#define CK(c) do { checks++; if (!(c)) { failed++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* gfx_soft.c to16: the word a U16 target stores for its value */
static uint32_t store16(float f) { return f <= 0 ? 0 : f >= 1 ? 65535 : (uint32_t)(f * 65535.f + .5f); }
/* gfx_soft.c apply_swizzle, SWIZZLE1: -1 = the value, else that constant */
static const float k1[8][4] = { {-1, 0, 0, 1}, {-1, 0, 0, 0}, {-1, 1, 1, 1}, {-1, -1, -1, -1}, {-1, -1, -1, 0}, {-1, -1, -1, 1}, {0, 0, 0, -1}, {1, 1, 1, -1} };
/* gfx_gpu.c view_mode's sw1 selectors for one-component textures (0 = the decoded value, 4 = zero, 5 = one) */
static const uint8_t sw1[8][4] = { {0,4,4,5}, {0,4,4,4}, {0,5,5,5}, {0,0,0,0}, {0,0,0,4}, {0,0,0,5}, {4,4,4,0}, {5,5,5,0} };

int main(void)
{
	const uint32_t tgt = 0x40800000u;   /* U16 colour target */
	for (uint32_t sw = 0; sw < 8; sw++) {
		const uint32_t tex = 0x09000000u | sw << 12 | 0x300u;   /* U16 texture, swizzle sw, stride 768 texels */
		uint32_t bpp = 4; int mode = view_u16_mode(tgt, tex, &bpp);
		CK(mode == (int)VIEW_U16_MODE); CK(bpp == 2);
		if (mode < 0) continue;
		int bad = 0;
		for (int k = 0; k < 256; k++) {
			const float r = (float)k / 255.f;                    /* what the GPU target's R holds (RGBA8) */
			const float dec = (float)store16(r) / 65535.f;      /* download -> guest word -> texture decode */
			float want[4], got[4], c[4] = { r, 0, 0, 1 };        /* the view shader's mode 6 input */
			for (int i = 0; i < 4; i++) want[i] = k1[sw][i] < 0 ? dec : k1[sw][i];
			for (int i = 0; i < 4; i++) got[i] = sw1[sw][i] < 4 ? c[sw1[sw][i]] : sw1[sw][i] == 4 ? 0.f : 1.f;
			for (int i = 0; i < 4; i++) if (fabsf(want[i] - got[i]) > 1e-7f) { if (bad++ < 4) printf("  swizzle %u k %d comp %d: want %.9f got %.9f\n", sw, k, i, want[i], got[i]); }
		}
		CK(bad == 0);
	}
	/* the pairs it leaves to others: refused, tex_bpp untouched */
	{ uint32_t b = 7; CK(view_u16_mode(0x40800000u, 0x0c000000u, &b) == -1 && b == 7); }   /* U16 target, U8U8U8U8 texture */
	{ uint32_t b = 7; CK(view_u16_mode(0x20800000u, 0x0a000000u, &b) == -1 && b == 7); }   /* S16 as S16 (not covered) */
	{ uint32_t b = 7; CK(view_u16_mode(0xF0000000u, 0x09000000u, &b) == -1 && b == 7); }   /* F16 target bits as U16 */
	{ uint32_t b = 7; CK(view_u16_mode(0x00000000u, 0x09000000u, &b) == -1 && b == 7); }   /* U8U8U8U8 target as U16 */
	{ uint32_t b = 7; CK(view_u16_mode(0x50800000u, 0x09000000u, &b) == -1 && b == 7); }   /* U16U16 target as U16 */
	printf("%d checks, %d failed\n", checks, failed);
	return failed ? 1 : 0;
}
