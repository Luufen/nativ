/*
 * win_sleep_test.c -- runtime/platform/win_sleep.h (Windows): a sleep returns after about what it asked for. A guest's
 * sceKernelDelayThread(1) (a yield in a spin-wait) reaches win_sleep_ns(1000); on the high-resolution waitable timer alone
 * it took ~0.5 ms (KZ-120: Killzone's missions spin-wait ~43 times a frame). Checks the mean of short sleeps: the 100 us
 * floor (a lock-poll loop's waiter must get in), well under the timer's ~500 us, and that a long sleep is still a real sleep (no early return, no core kept for it).
 *
 * -DWIN_SLEEP_LEGACY: the timer for every sleep, as before -- the fail-before.
 *
 *   x86_64-w64-mingw32-clang -O1 -Wall -I ../platform win_sleep_test.c -o win_sleep_test.exe && ./win_sleep_test.exe
 *   x86_64-w64-mingw32-clang -O1 -Wall -I ../platform -DWIN_SLEEP_LEGACY win_sleep_test.c -o win_sleep_legacy.exe && ./win_sleep_legacy.exe
 */
#include <stdio.h>
#include <windows.h>
#include <timeapi.h>
#include "win_sleep.h"

static int checks, failed;
#define CHECK(c, ...) do { checks++; if (!(c)) { failed++; printf("FAIL line %d: %s: ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double mean_us(uint64_t ns, int n) {
	uint64_t t0 = win_sleep_now_ns();
	for (int i = 0; i < n; i++) win_sleep_ns(ns);
	return (double)(win_sleep_now_ns() - t0) / n / 1000.0;
}
static double cpu_ms(void) {
	FILETIME c, e, k, u; GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u);
	return (double)((((uint64_t)k.dwHighDateTime << 32) | k.dwLowDateTime) + (((uint64_t)u.dwHighDateTime << 32) | u.dwLowDateTime)) / 1e4;
}
int main(void) {
	timeBeginPeriod(1);   /* (as the runtime: platform_win32.c install) */
	double y = mean_us(1000, 2000), m = mean_us(200000, 500), z = mean_us(0, 2000);
	printf("mean sleep: 1 us -> %.1f us, 200 us -> %.1f us, 0 -> %.1f us\n", y, m, z);
	CHECK(y >= 99 && y < 200, "DelayThread(1) took %.1f us (the 100 us floor, not the timer's ~500)", y);
	CHECK(m >= 199 && m < 300, "DelayThread(200) took %.1f us", m);
	CHECK(z >= 99 && z < 200, "DelayThread(0) took %.1f us", z);
	double c0 = cpu_ms(), l = mean_us(20000000, 10), c1 = cpu_ms();
	printf("mean sleep: 20 ms -> %.2f ms, thread CPU %.1f ms for 10 of them\n", l / 1000, c1 - c0);
	CHECK(l >= 19900 && l < 25000, "a 20 ms sleep took %.1f us", l);
	CHECK(c1 - c0 < 20, "the 20 ms sleeps used %.1f ms of CPU (must wait, not spin)", c1 - c0);
	printf("%d checks, %d failed\n", checks, failed);
	return failed != 0;
}
