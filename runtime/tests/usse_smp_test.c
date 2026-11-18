/*
 * Synthetic USSE SMP fixture: dependent samplers and texture-buffer indexing in the GPU translator.
 *
 * A dependent sampler names one SA slot whose per-draw table resolves to a constant texture unit.
 * A texture-buffer SMP instead reads the low byte of TEMP/PA at shader run time. SDL_GPU binds the
 * Vita units separately, so the SPIR-V translator emits a structured switch with one statically
 * named compatible 2D or cube sampler per case and an OpPhi result. For implicit LOD, coordinate
 * derivatives are taken before the switch and each case uses an explicit gradient sample, keeping
 * derivatives out of non-uniform control flow.
 *
 *     cc -O1 -g -Wall -D_GNU_SOURCE -I ../gfx -I ../core -I ../../recompiler -I ../.. usse_smp_test.c ../gfx/usse.c ../gfx/usse_spirv.c -o /tmp/usse_smp_test -lm && /tmp/usse_smp_test
 *     (SPIRV_VAL=<spirv-val> also validates every successful module.)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "usse.h"
#include "usse_internal.h"
#include "usse_spirv.h"

uint8_t *recomp_mem;
uint32_t recomp_watch_lo, recomp_watch_hi;
void recomp_watch_write(uint32_t a, uint32_t v, int width) { (void)a; (void)v; (void)width; }

static int checks, failed;
static void ck(int cond, const char *what) { checks++; printf("%-78s %s\n", what, cond ? "ok" : "FAIL"); if (!cond) failed++; }

#define GXP_ADDR 0x10000000u
#define H_FLAGS 20
#define H_PRIM_REGS 48
#define H_PRIM_INSTR_COUNT 60
#define H_PRIM_OFF 64
#define H_THREAD_SIZE 84
#define H_TEXTURE_SIZE 96
#define H_UBINFO_COUNT 120
#define H_UBINFO_OFF 124
#define H_DEPSAMP_COUNT 128
#define H_DEPSAMP_OFF 132
#define H_TEXDEPSAMP_COUNT 136
#define H_TEXDEPSAMP_OFF 140
#define H_CONTAINER_COUNT 144
#define H_CONTAINER_OFF 148
#define CODE_OFF 128
static void put16(uint32_t a, uint16_t v) { memcpy(recomp_mem + a, &v, 2); }
static void put32(uint32_t a, uint32_t v) { memcpy(recomp_mem + a, &v, 4); }
static void put64(uint32_t a, uint64_t v) { memcpy(recomp_mem + a, &v, 8); }

/* 2D F32 SMP to TEMP8. Coordinates are TEMP2, explicit LOD is TEMP4. src1_bank:
 * 0 -> TEMP0 (dynamic); 3 -> SA4 (dependent). */
static uint64_t smp(int src1_bank, int lod_mode, int dim) {
	return 0xE000000000000000ull | 3ull << 46 | (uint64_t)dim << 42 | (uint64_t)lod_mode << 40 |
	       8ull << 21 | 1ull << 14 | (uint64_t)src1_bank << 30 | (src1_bank == 3 ? 2ull << 7 : 0) | 2ull;
}
static UsseProgram *load_words(const uint64_t *words, int n) {
	static uint32_t seq;
	uint32_t g = GXP_ADDR + (++seq) * 0x10000;
	memset(recomp_mem + g, 0, CODE_OFF + 64);
	put32(g + 8, CODE_OFF + (uint32_t)n * 8);
	put32(g + H_FLAGS, 1);
	put32(g + H_PRIM_REGS, 16);
	put32(g + H_PRIM_INSTR_COUNT, (uint32_t)n);
	put32(g + H_PRIM_OFF, CODE_OFF - H_PRIM_OFF);
	for (int i = 0; i < n; i++) put64(g + CODE_OFF + (uint32_t)i * 8, words[i]);
	return usse_decode_uncached(g);
}
static UsseProgram *load(uint64_t word) { return load_words(&word, 1); }
static UsseProgram *load_thread_words(const uint64_t *words, int n) {
	enum { META_CONTAINER = 176, META_UBINFO = 184, THREAD_CODE = 256 };
	static uint32_t seq;
	uint32_t g = GXP_ADDR + 0x01000000u + (++seq) * 0x10000;
	memset(recomp_mem + g, 0, THREAD_CODE + (size_t)n * 8);
	put32(g + 8, THREAD_CODE + (uint32_t)n * 8);
	put32(g + H_FLAGS, 1);
	put32(g + H_PRIM_REGS, 16);
	put32(g + H_PRIM_INSTR_COUNT, (uint32_t)n);
	put32(g + H_PRIM_OFF, THREAD_CODE - H_PRIM_OFF);
	put32(g + H_THREAD_SIZE, 4 * 64);   /* four private words: header bytes divided by 64 */
	put32(g + H_CONTAINER_COUNT, 1);
	put32(g + H_CONTAINER_OFF, META_CONTAINER - H_CONTAINER_OFF);
	put16(g + META_CONTAINER, 19); put16(g + META_CONTAINER + 4, 4); put16(g + META_CONTAINER + 6, 1);
	put32(g + H_UBINFO_COUNT, 1);
	put32(g + H_UBINFO_OFF, META_UBINFO - H_UBINFO_OFF);
	put16(g + META_UBINFO, 18); put16(g + META_UBINFO + 2, 0);
	for (int i = 0; i < n; i++) put64(g + THREAD_CODE + (uint32_t)i * 8, words[i]);
	return usse_decode_uncached(g);
}
static UsseProgram *load_sampler_words(const uint64_t *words, int n, int texture_buffer) {
	enum { META_CONTAINER = 176, META_UBINFO = 184, META_DEPSAMP = 192, META_TEXDEPSAMP = 196, TEXTURE_CODE = 256 };
	static uint32_t seq;
	uint32_t g = GXP_ADDR + 0x02000000u + (++seq) * 0x10000;
	memset(recomp_mem + g, 0, TEXTURE_CODE + (size_t)n * 8);
	put32(g + 8, TEXTURE_CODE + (uint32_t)n * 8);
	put32(g + H_FLAGS, 1);
	put32(g + H_PRIM_REGS, 16);
	put32(g + H_PRIM_INSTR_COUNT, (uint32_t)n);
	put32(g + H_PRIM_OFF, TEXTURE_CODE - H_PRIM_OFF);
	if (texture_buffer) put32(g + H_TEXTURE_SIZE, 64);
	put32(g + H_CONTAINER_COUNT, 1);
	put32(g + H_CONTAINER_OFF, META_CONTAINER - H_CONTAINER_OFF);
	put16(g + META_CONTAINER, 19); put16(g + META_CONTAINER + 4, 4); put16(g + META_CONTAINER + 6, 8);
	if (texture_buffer) {
		put32(g + H_UBINFO_COUNT, 1);
		put32(g + H_UBINFO_OFF, META_UBINFO - H_UBINFO_OFF);
		put16(g + META_UBINFO, 15); put16(g + META_UBINFO + 2, 0); put32(g + META_UBINFO + 4, 0xFFFFFFFCu);
		put32(g + H_TEXDEPSAMP_COUNT, 4);
		put32(g + H_TEXDEPSAMP_OFF, META_TEXDEPSAMP - H_TEXDEPSAMP_OFF);
		for (int word = 0; word < 4; word++) {
			put16(g + META_TEXDEPSAMP + 4 * word, (uint16_t)(12 + word));
			put16(g + META_TEXDEPSAMP + 4 * word + 2, (uint16_t)(20 + word));
		}
	}
	put32(g + H_DEPSAMP_COUNT, 1);
	put32(g + H_DEPSAMP_OFF, META_DEPSAMP - H_DEPSAMP_OFF);
	put16(g + META_DEPSAMP, 12); put16(g + META_DEPSAMP + 2, 1);
	for (int i = 0; i < n; i++) put64(g + TEXTURE_CODE + (uint32_t)i * 8, words[i]);
	return usse_decode_uncached(g);
}
/* U32 LDST TEMP0, [SA4 + #0 + #0], with MOE expansion disabled. */
static uint64_t ldst(void) {
	return 0x1Dull << 59 | 1ull << 53 | 1ull << 50 | 1ull << 49 | 1ull << 48 |
	       1ull << 34 | 2ull << 30 | 2ull << 28 | 4ull << 14;
}
static uint64_t ldst_texture_word(int slot, int word) { return ldst() | (uint64_t)(4 * slot + 1 + word) << 7; }
/* U32 store [SA4 + TEMP0] = TEMP1. */
static uint64_t ldst_thread_store(void) {
	return 0x1Eull << 59 | 1ull << 53 | 1ull << 50 | 1ull << 34 | 4ull << 14 | 1ull;
}
static int sampled_unit;
static void capture_unit(void *ctx, int unit, float u, float v, float w, float lod, float out[4]) {
	(void)ctx; (void)u; (void)v; (void)w; (void)lod;
	sampled_unit = unit;
	out[0] = out[1] = out[2] = 0; out[3] = 1;
}
static int count_op(const SpirvModule *m, uint32_t op) {
	int c = 0;
	for (int i = 5; i < m->nwords; ) {
		uint32_t wc = m->words[i] >> 16;
		if ((m->words[i] & 0xffff) == op) c++;
		if (!wc) break;
		i += (int)wc;
	}
	return c;
}
static int validate(const SpirvModule *m, const char *name) {
	const char *val = getenv("SPIRV_VAL"); if (!val || !*val) return 1;
	const char *dir = getenv("SPIRV_TMP") ? getenv("SPIRV_TMP") : "/tmp";
	char path[256]; snprintf(path, sizeof path, "%s/usse_smp_%s.spv", dir, name);
	FILE *f = fopen(path, "wb"); if (!f) return 0;
	fwrite(m->words, 4, (size_t)m->nwords, f); fclose(f);
	char cmd[600]; snprintf(cmd, sizeof cmd, "%s %s", val, path);
	int rc = system(cmd); remove(path); return rc == 0;
}
static SpirvModule translate(uint64_t word, uint32_t sa[256], int units[16], int *nsmp, uint32_t cube_mask) {
	UsseProgram *p = load(word);
	SpirvModule m = usse_spirv_translate(p, 0, usse_fragment_layout(p), sa, 16, units, nsmp, 0, cube_mask, 0, 0);
	usse_program_free(p);
	return m;
}
static SpirvModule translate_pair(uint64_t first, uint64_t second, uint32_t sa[256], int units[16], int *nsmp) {
	uint64_t words[2] = {first, second};
	UsseProgram *p = load_words(words, 2);
	SpirvModule m = usse_spirv_translate(p, 0, usse_fragment_layout(p), sa, 16, units, nsmp, 0, 0, 0, 0);
	usse_program_free(p);
	return m;
}

int main(void) {
#ifdef _WIN32
	recomp_mem = VirtualAlloc(0, 512u << 20, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!recomp_mem) { fprintf(stderr, "VirtualAlloc failed\n"); return 2; }
#else
	recomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (recomp_mem == MAP_FAILED) { perror("mmap"); return 2; }
#endif
	usse_spirv_set_canonical_varyings(1);

	uint32_t sa[256] = {0}; int units[16] = {0}, nsmp = 0;
	usse_spirv_set_dynamic_smp(0);   /* the default: a register-indexed SMP refuses the module (software path) */
	SpirvModule dyn_off = translate(smp(0, 2, 1), sa, units, &nsmp, 0);
	ck(!dyn_off.ok, "without the opt-in a register-indexed SMP is refused, as before");
	usse_spirv_free(&dyn_off);
	usse_spirv_set_dynamic_smp(1);   /* RECOMP_GPU_DYNAMIC_SMP=1: the rest of this test */
	SpirvModule dyn = translate(smp(0, 2, 1), sa, units, &nsmp, 0);
	ck(dyn.ok, "TEMP0 texture-buffer index translates");
	if (!dyn.ok) printf("     err: %s\n", dyn.err);
	ck(dyn.ok && nsmp == 16, "dynamic SMP declares all 16 compatible texture units");
	ck(dyn.ok && count_op(&dyn, 251) == 1 && count_op(&dyn, 245) == 1, "dynamic SMP emits one OpSwitch and one OpPhi");
	ck(!dyn.ok || validate(&dyn, "dynamic"), "spirv-val accepts the dynamic module (or SPIRV_VAL is unset)");
	usse_spirv_free(&dyn);

	memset(units, 0, sizeof units); nsmp = 0; sa[4] = 3;
	SpirvModule dep = translate(smp(3, 2, 1), sa, units, &nsmp, 0);
	ck(dep.ok && nsmp == 1 && units[0] == 3, "dependent SA4 resolves to one texture-unit-3 binding");
	ck(dep.ok && count_op(&dep, 251) == 0, "dependent sampler keeps the direct sample path");
	ck(!dep.ok || validate(&dep, "dependent"), "spirv-val accepts the dependent module (or SPIRV_VAL is unset)");
	usse_spirv_free(&dep);

	memset(units, 0, sizeof units); nsmp = 0;
	SpirvModule implicit = translate(smp(0, 0, 1), sa, units, &nsmp, 0);
	ck(implicit.ok && nsmp == 16, "dynamic implicit-LOD sampling translates");
	ck(implicit.ok && count_op(&implicit, 207) >= 2 && count_op(&implicit, 208) >= 2, "implicit derivatives are emitted before the sampler switch");
	ck(!implicit.ok || validate(&implicit, "implicit"), "spirv-val accepts the implicit-LOD module (or SPIRV_VAL is unset)");
	usse_spirv_free(&implicit);

	memset(units, 0, sizeof units); nsmp = 0;
	SpirvModule cube = translate(smp(0, 0, 2), sa, units, &nsmp, 1u << 3);
	ck(cube.ok && nsmp == 1 && units[0] == 3, "dynamic cube SMP binds compatible cube units");
	ck(!cube.ok || validate(&cube, "cube"), "spirv-val accepts the dynamic cube module (or SPIRV_VAL is unset)");
	usse_spirv_free(&cube);

	memset(units, 0, sizeof units); nsmp = 0;
	SpirvModule ldst_dynamic = translate_pair(smp(0, 2, 1), ldst(), sa, units, &nsmp);
	ck(ldst_dynamic.ok && ldst_dynamic.nldst_sa == 1 && ldst_dynamic.ldst_sa[0] == 4, "fragment LDST traces its direct SA4 guest pointer");
	ck(ldst_dynamic.ok && nsmp == 16 && ldst_dynamic.frag_ldst_storage, "16-sampler fragment LDST uses a storage-only window variant");
	ck(!ldst_dynamic.ok || validate(&ldst_dynamic, "ldst_dynamic"), "spirv-val accepts the storage-only fragment LDST module");
	usse_spirv_free(&ldst_dynamic);

	memset(units, 0, sizeof units); nsmp = 0; sa[4] = 3;
	SpirvModule ldst_target = translate_pair(smp(3, 2, 1), ldst(), sa, units, &nsmp);
	ck(ldst_target.ok && nsmp == 1 && !ldst_target.frag_ldst_storage, "small fragment LDST modules retain the target-image fast path");
	ck(ldst_target.ok && count_op(&ldst_target, 247) >= 1 && count_op(&ldst_target, 245) >= 1, "hybrid fragment LDST emits structured resource selection");
	ck(!ldst_target.ok || validate(&ldst_target, "ldst_target"), "spirv-val accepts the hybrid fragment LDST module");
	usse_spirv_free(&ldst_target);

	{
		uint64_t words[2] = {ldst_thread_store(), ldst() | 2ull << 21};
		UsseProgram *p = load_thread_words(words, 2);
		int32_t bias = -1; uint32_t thread_words = 0;
		ck(usse_thread_buffer_info(p, 4, &bias, &thread_words) && bias == 0 && thread_words == 4,
		   "container-18 metadata maps SA4 to four private words");
		UsseDraw draw = {0}; UsseInst inst = {0};
		usse_setup_registers(p, &draw, 0, 0);
		inst.r[0].u = 0x10000;   /* register offsets carry a 0x10000 bias */
		inst.r[1].u = 0xDEADBEEFu;
		usse_run_primary(p, &inst, &draw, 0, 0);
		ck(inst.r[2].u == 0xDEADBEEFu, "interpreter round-trips an LDST store/load through private thread memory");
		memset(units, 0, sizeof units); nsmp = 0;
		SpirvModule thread = usse_spirv_translate(p, 0, usse_fragment_layout(p), sa, 16, units, &nsmp, 0, 0, 0, 0);
		ck(thread.ok && thread.nldst_sa == 0 && !thread.frag_ldst_storage,
		   "thread-buffer LDST needs no guest-memory or target resource");
		ck(thread.ok && count_op(&thread, 62) >= 1, "thread-buffer store emits an OpStore to private memory");
		ck(!thread.ok || validate(&thread, "ldst_thread"), "spirv-val accepts the private thread-buffer module");
		usse_spirv_free(&thread);
		usse_program_free(p);
	}
	{
		uint64_t dependent = smp(3, 2, 1);
		UsseProgram *p = load_sampler_words(&dependent, 1, 0);
		UsseDraw draw = {0};
		usse_setup_registers(p, &draw, 0, 0);
		ck(draw.sa[5].u == 0 && draw.sa_unit[5] == 4,
		   "dependent sampler metadata stays numeric zero without a texture buffer");
		usse_program_free(p);
	}
	{
		uint64_t words[2] = {ldst_texture_word(5, 0), smp(0, 2, 1)};
		UsseProgram *p = load_sampler_words(words, 2, 1);
		int32_t bias = 0; uint32_t texture_words = 0;
		ck(usse_texture_buffer_info(p, 4, &bias, &texture_words) && bias == -4 && texture_words == 64,
		   "container-15 metadata maps SA4 to 64 synthetic texture words with bias -4");
		ck(usse_texture_buffer_word(p, 20) == 3 && usse_texture_buffer_word(p, 12) == 0,
		   "texture-buffer permutation maps descriptor slot 5 to texture unit 3");
		UsseDraw draw = {0}; UsseInst inst = {0};
		usse_setup_registers(p, &draw, 0, 0);
		ck(draw.sa[4].u == 0xFFFFFFFCu, "texture-buffer SA receives its signed pointer bias");
		ck(draw.sa[5].u == 3 && draw.sa_unit[5] == 4,
		   "dependent sampler word zero carries texture unit 3 when a texture buffer exists");
		sampled_unit = -1;
		usse_run_primary(p, &inst, &draw, 0, capture_unit);
		ck(sampled_unit == 3, "interpreter texture-buffer LDST selects texture unit 3");
		uint32_t tsa[256] = {0};
		for (int i = 0; i < 256; i++) tsa[i] = draw.sa_unit[i] ? draw.sa_unit[i] - 1u : draw.sa[i].u;
		memset(units, 0, sizeof units); nsmp = 0;
		SpirvModule texture = usse_spirv_translate(p, 0, usse_fragment_layout(p), tsa, 16, units, &nsmp, 0, 0, 0, 0);
		ck(texture.ok && texture.nldst_sa == 0 && !texture.frag_ldst_storage,
		   "texture-buffer LDST needs no guest-memory or target resource");
		ck(texture.ok && nsmp == 16, "LDST-derived sampler index keeps the dynamic 16-unit SPIR-V path");
		ck(!texture.ok || validate(&texture, "ldst_texture"), "spirv-val accepts the texture-buffer module");
		usse_spirv_free(&texture);
		usse_program_free(p);
	}
	for (int word = 1; word < 4; word++) {
		uint64_t words[2] = {ldst_texture_word(5, word), smp(0, 2, 1)};
		UsseProgram *p = load_sampler_words(words, 2, 1);
		UsseDraw draw = {0}; UsseInst inst = {0};
		usse_setup_registers(p, &draw, 0, 0);
		sampled_unit = -1;
		usse_run_primary(p, &inst, &draw, 0, capture_unit);
		char what[96]; snprintf(what, sizeof what, "texture descriptor word %d reads as zero", word);
		ck(sampled_unit == 0, what);
		usse_program_free(p);
	}

	memset(units, 0, sizeof units); nsmp = 0;
	SpirvModule gradients = translate(smp(0, 3, 1), sa, units, &nsmp, 0);
	ck(!gradients.ok && strstr(gradients.err, "explicit gradients") != 0, "dynamic explicit-gradient layout is refused explicitly");
	usse_spirv_free(&gradients);

	printf("%d checks, %d failed\n", checks, failed);
	if (failed) return 1;
	printf("all passed\n");
	return 0;
}
