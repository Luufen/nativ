/*
 * The rows of a render target a texture's bytes lie in -- for a partial download (gfx_gpu.c target_download_rows).
 *
 * A texture that reads guest memory inside a GPU-drawn target, in a layout no GPU view serves, takes the guest-memory
 * path: the target's GPU pixels go down to guest memory first. When the texture is small and the target big (a 32x32
 * U8 texture inside a 768x1024 U8U8U8U8 target: 1 KB of 3 MB), only the rows that hold its bytes need to come down;
 * the rest of the target stays GPU-newer (gpu_dirty), exactly as before the read.
 *
 * Header-only C, no state (runtime/tests/target_rows_test.c).
 */
#ifndef TARGET_ROWS_H
#define TARGET_ROWS_H
#include <stdint.h>

/* the target's rows [*y0, *y1) that cover the bytes [lo, hi) of a target at `addr`, `row_bytes` a row, `height` rows;
 * returns 1 when that is a partial download worth taking (the rows are at most a quarter of the target), else 0 (no
 * overlap, a bad argument, or most of the target anyway: download it whole) */
static inline int target_rows_for(uint32_t addr, uint32_t row_bytes, uint32_t height, uint32_t lo, uint32_t hi, uint32_t *y0, uint32_t *y1) {
	if (!row_bytes || !height || hi <= lo) return 0;
	const uint64_t end = (uint64_t)addr + (uint64_t)row_bytes * height;
	if ((uint64_t)hi <= addr || (uint64_t)lo >= end) return 0;
	const uint64_t a = lo > addr ? (uint64_t)(lo - addr) : 0, b = ((uint64_t)hi < end ? (uint64_t)hi : end) - addr;
	const uint32_t r0 = (uint32_t)(a / row_bytes), r1 = (uint32_t)((b + row_bytes - 1) / row_bytes);
	if (r1 <= r0 || (uint64_t)(r1 - r0) * 4 > height) return 0;
	*y0 = r0; *y1 = r1;
	return 1;
}
#endif
