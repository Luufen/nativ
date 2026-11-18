/* view_u16.h -- a GPU view of a U16 render target read by a U16 texture (gfx_gpu.c view_mode). The target's GPU
 * texture holds its colour as rendered (its one value in R); guest memory holds what gfx_soft.c's target store
 * writes for a U16 target, to16(R) = round(R * 65535) as one 16-bit word a pixel; a U16 texture over that memory
 * decodes word / 65535 into one component its SWIZZLE1 selectors place. The view returns R directly (view mode 6:
 * one component at full precision, into an RGBA32F view texture), placed by the same selectors -- the value the
 * download + decode round trip gives, without the round trip. Before, a U16 target had no view mode and every read
 * downloaded the whole target: a shadow-atlas style surface sampled as sub-rectangles (several reads a frame of one
 * 768x768 U16 target, each a synchronous GPU->CPU copy of 5-20 ms) cost a title two thirds of its frame time.
 * Pure, header-only: runtime/tests/view_u16_test.c checks it against gfx_soft's store and decode restated. */
#ifndef VIEW_U16_H
#define VIEW_U16_H
#include <stdint.h>

#define VIEW_U16_MODE 6u   /* gfx_gpu.c's view shader: swizzle(float4(c.r, 0, 0, 1)) at full precision */

/* tgt_fmt: the colour target's format; tex_fmt: the texture's. The view mode (VIEW_U16_MODE) and *tex_bpp = 2
 * when the texture reads a U16 target as U16; -1 (and *tex_bpp untouched) for any other pair */
static inline int view_u16_mode(uint32_t tgt_fmt, uint32_t tex_fmt, uint32_t *tex_bpp) {
	if ((tgt_fmt & 0xF1800000u) != 0x40800000u || (tex_fmt & 0xff000000u) != 0x09000000u) return -1;
	*tex_bpp = 2;
	return (int)VIEW_U16_MODE;
}
#endif
