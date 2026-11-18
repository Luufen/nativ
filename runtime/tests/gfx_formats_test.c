/* gfx_formats_test.c -- texture and colour-target formats of the software sampler (runtime/gfx/gfx_soft.c), through which
 * the GPU path also decodes guest textures (gfx_decode_texture_f32) and round-trips render targets (gfx_encode/decode_
 * target_rgba8). Synthetic pixels in a small guest arena, no game data.
 *
 * Contracts (SceGxm formats):
 *  - texture base 0x0e U2U10U10U10, SWIZZLE4 ABGR: R in bits 0-9, G 10-19, B 20-29, A 30-31 (A /3);
 *  - texture base 0x13 F32M: one 32-bit float component (placed by the SWIZZLE1 modes, as F32);
 *  - texture base 0x19 SE5M9M9M9: R, G and B are 9-bit mantissas in ascending bit order, scaled by a shared
 *    5-bit exponent in bits 27-31; each component is mantissa * 2^(exponent - 24);
 *  - colour format 0x60800000 U2U10U10U10: the same fields, in the 8-bit formats' component orders; what a render target
 *    stores in this format reads back as a 0x0e texture with the same values.
 * Observed (AUTO-01 GR-13): Gravity Rush renders its scene into a 0x60800000 target and composites it as a 0x0e texture;
 * neither was known, every texel came out as the fallback magenta (1, 0, 1, 1), and so did the whole frame.
 *
 *   I="-I ../core -I ../gfx -I ../audio -I ../media -I ../input -I ../net -I ../ui -I ../platform -I ../debug -I ../../recompiler -I ../.."
 *   cc -O1 -g -w -D_GNU_SOURCE $I gfx_formats_test.c -o /tmp/gfx_formats_test -lm -lpthread -Wl,--unresolved-symbols=ignore-all && /tmp/gfx_formats_test
 * (the unresolved symbols are the renderer's shader/window entry points, which these checks never reach)
 */
#include <sys/mman.h>
#include "../gfx/gfx_soft.c"

uint8_t *recomp_mem;
uint32_t recomp_watch_lo, recomp_watch_hi;
void recomp_watch_write(uint32_t a, uint32_t v, int w) { (void)a; (void)v; (void)w; }

static int fails, n;
static void check(const char *what, int ok) { n++; printf("%-80s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }
static int near(float a, float b) { return fabsf(a - b) < 0.002f; }

int main(void) {
	recomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (recomp_mem == MAP_FAILED) { puts("no arena"); return 2; }
	const uint32_t T = 0x81000000u;
	/* U2U10U10U10 texture, 2x1, linear, stride 8: (R 1023, G 512, B 0, A 3) and (R 0, G 0, B 1023, A 1) */
	WR32(T, 1023u | 512u << 10 | 0u << 20 | 3u << 30);
	WR32(T + 4, 0u | 0u << 10 | 1023u << 20 | 1u << 30);
	GfxTexture t; memset(&t, 0, sizeof t);
	t.data = T; t.width = 2; t.height = 1; t.format = 0x0e000000u | 8;   /* base 0x0e, swizzle 0 (ABGR), stride 8 */
	float px[8];
	gfx_decode_texture_f32(&t, px);
	check("U2U10U10U10 texel 0: R 1, G 512/1023, B 0, A 1", near(px[0], 1) && near(px[1], 512 / 1023.f) && near(px[2], 0) && near(px[3], 1));
	check("U2U10U10U10 texel 1: R 0, G 0, B 1, A 1/3", near(px[4], 0) && near(px[5], 0) && near(px[6], 1) && near(px[7], 1 / 3.f));
	check("U2U10U10U10 is not the magenta fallback", !(px[0] == 1 && px[1] == 0 && px[2] == 1));
	t.format = 0x0e000000u | 0x1000u | 8;   /* swizzle 1 (ARGB): R and B swap places */
	gfx_decode_texture_f32(&t, px);
	check("U2U10U10U10 ARGB swizzle swaps R and B", near(px[0], 0) && near(px[2], 1));

	/* F32M texture, 1x1: 0.625 in R (SWIZZLE1 _R: 0, 0, 1 elsewhere) */
	float f = 0.625f; uint32_t fb; memcpy(&fb, &f, 4); WR32(T + 0x100, fb);
	t.data = T + 0x100; t.width = 1; t.height = 1; t.format = 0x13000000u | 8;
	gfx_decode_texture_f32(&t, px);
	check("F32M texel: R 0.625 (as F32), G 0, B 0, A 1", near(px[0], 0.625f) && near(px[1], 0) && near(px[2], 0) && near(px[3], 1));
	check("F32M is 4 bytes a texel (the GPU upload's size)", gfx_texture_bpp(&t) == 4);

	/* SE5M9M9M9 texture, 2x1: (1, 0.5, 0.25) and the format's maximum finite value in every channel */
	WR32(T + 0x180, 256u | 128u << 9 | 64u << 18 | 16u << 27);
	WR32(T + 0x184, 511u | 511u << 9 | 511u << 18 | 31u << 27);
	t.data = T + 0x180; t.width = 2; t.height = 1; t.format = 0x19000000u | 8;
	gfx_decode_texture_f32(&t, px);
	check("SE5M9M9M9 texel 0: R 1, G 0.5, B 0.25, A 1", near(px[0], 1) && near(px[1], 0.5f) && near(px[2], 0.25f) && near(px[3], 1));
	check("SE5M9M9M9 texel 1: maximum finite 65408 in RGB", px[4] == 65408 && px[5] == 65408 && px[6] == 65408 && px[7] == 1);
	check("SE5M9M9M9 is 4 bytes a texel (the GPU upload's size)", gfx_texture_bpp(&t) == 4);
	t.format = 0x19000000u | 0x1000u | 8;
	gfx_decode_texture_f32(&t, px);
	check("SE5M9M9M9 RGB swizzle swaps R and B", near(px[0], 0.25f) && near(px[1], 0.5f) && near(px[2], 1));
	/* The matching colour target must store packed RGB rather than falling through to RGBA8, where black with A=1
	 * became 0xff000000 and decoded through the shared exponent as a nearly-white HDR value. */
	GfxTarget se; memset(&se, 0, sizeof se); se.data = T + 0x1c0; se.width = 2; se.height = 1; se.stride = 2; se.format = 0x31000000u;
	float sei[8] = {1, 0.5f, 0.25f, 0.125f, 0, 0, 0, 1}, seo[8];
	gfx_encode_target_f32(&se, sei);
	check("SE5M9M9M9 target stores (1, 0.5, 0.25) in the shared-exponent word", RD32(T + 0x1c0) == (256u | 128u << 9 | 64u << 18 | 16u << 27));
	check("SE5M9M9M9 target stores black as zero regardless of alpha", RD32(T + 0x1c4) == 0);
	gfx_decode_target_f32(&se, seo);
	check("SE5M9M9M9 target round trip restores RGB and supplies alpha 1",
	      near(seo[0], 1) && near(seo[1], 0.5f) && near(seo[2], 0.25f) && seo[3] == 1 && seo[4] == 0 && seo[5] == 0 && seo[6] == 0 && seo[7] == 1);
	check("SE5M9M9M9 target is 4 bytes a pixel", gfx_target_bpp(&se) == 4);

	/* F32F32 texture (base 0x1e), 2x1: two 32-bit floats a texel, R then G, sampled as (r, g, 0, 1) */
	{ float v[4] = {0.25f, -3.5f, 1024.0f, 0.125f}; for (int k = 0; k < 4; k++) { uint32_t w; memcpy(&w, &v[k], 4); WR32(T + 0x300 + 4 * k, w); }
	  GfxTexture f2; memset(&f2, 0, sizeof f2); f2.data = T + 0x300; f2.width = 2; f2.height = 1; f2.format = 0x1e000000u | 8;
	  float q[8]; gfx_decode_texture_f32(&f2, q);
	  check("F32F32 texel 0: R 0.25, G -3.5, B 0, A 1", q[0] == 0.25f && q[1] == -3.5f && q[2] == 0 && q[3] == 1);
	  check("F32F32 texel 1: R 1024, G 0.125 (8 bytes on, full float range)", q[4] == 1024.0f && q[5] == 0.125f && q[6] == 0 && q[7] == 1);
	  check("F32F32 is 8 bytes a texel (the GPU upload's size)", gfx_texture_bpp(&f2) == 8);
	  /* an F32F32 colour target (0x11000000) read back as an F32F32 texture: the values the target stored */
	  GfxTarget g2; memset(&g2, 0, sizeof g2); g2.data = T + 0x400; g2.width = 2; g2.height = 1; g2.stride = 2; g2.format = 0x11000000u;
	  float in2[8] = {0.5f, 2.0f, 9, 9, -1.0f, 3.0f, 9, 9}, q2[8];
	  gfx_encode_target_f32(&g2, in2);
	  f2.data = T + 0x400; gfx_decode_texture_f32(&f2, q2);
	  check("target 0x11000000 F32F32 read as texture 0x1e: (0.5, 2), (-1, 3)", q2[0] == 0.5f && q2[1] == 2.0f && q2[4] == -1.0f && q2[5] == 3.0f && q2[3] == 1 && q2[7] == 1); }

	/* a U2U10U10U10 colour target: what it stores, the 0x0e texture reads */
	GfxTarget g; memset(&g, 0, sizeof g);
	g.data = T + 0x200; g.width = 2; g.height = 1; g.stride = 2; g.format = 0x60800000u;
	uint8_t in[8] = {255, 128, 0, 255, 0, 0, 255, 0}, out[8];
	gfx_encode_target_rgba8(&g, in);
	uint32_t w0 = RD32(T + 0x200), w1 = RD32(T + 0x204);
	check("target 0x60800000 stores pixel 0 as U2U10U10U10 (R 1023, G 514, B 0, A 3)", w0 == (1023u | 514u << 10 | 3u << 30));
	check("target 0x60800000 stores pixel 1 (R 0, G 0, B 1023, A 0)", w1 == (1023u << 20));
	gfx_decode_target_rgba8(&g, out);
	check("target round trip: the RGBA8 the GPU path keeps comes back", !memcmp(in, out, 8));
	t.data = T + 0x200; t.width = 2; t.height = 1; t.format = 0x0e000000u | 8;
	gfx_decode_texture_f32(&t, px);
	check("the target's memory read as a 0x0e texture: the same colours",
	      near(px[0], 1) && near(px[1], 514 / 1023.f) && near(px[2], 0) && near(px[3], 1) && near(px[6], 1) && near(px[7], 0));

	/* A no-mip cube's swizzled faces are tightly packed. A 4x4 RGBA8 face is 64 bytes, not the
	 * 128 bytes a linear texture's width-rounded row stride would imply. */
	{
		static const uint8_t col[6][4] = {
			{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255},
			{255, 255, 0, 255}, {255, 0, 255, 255}, {0, 255, 255, 255}
		};
		static const float dir[6][3] = {
			{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
		};
		GfxTexture cube; memset(&cube, 0, sizeof cube);
		cube.data = T + 0x1000; cube.width = cube.height = 4; cube.format = 0x0c000000u;
		cube.u_mode = 0x32; cube.v_mode = 2;
		for (int face = 0; face < 6; face++) {
			uint32_t word = col[face][0] | (uint32_t)col[face][1] << 8 | (uint32_t)col[face][2] << 16 | (uint32_t)col[face][3] << 24;
			for (int i = 0; i < 16; i++) WR32(cube.data + 64u * (uint32_t)face + 4u * (uint32_t)i, word);
		}
		for (int face = 0; face < 6; face++) {
			float c[4]; char what[80];
			cube_texel(&cube, dir[face][0], dir[face][1], dir[face][2], 0, c);
			snprintf(what, sizeof what, "4x4 no-mip cube samples tightly packed face %d", face);
			check(what, near(c[0], col[face][0] / 255.f) && near(c[1], col[face][1] / 255.f) &&
			            near(c[2], col[face][2] / 255.f) && near(c[3], col[face][3] / 255.f));
		}
		GfxTexture face5; gfx_texture_face(&cube, 5, &face5);
		check("4x4 no-mip cube face offsets advance by 64 bytes", face5.data == cube.data + 5 * 64);
	}
	printf("%d checks, %d failed\n", n, fails);
	return fails ? 1 : 0;
}
