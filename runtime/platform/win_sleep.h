/* win_sleep.h -- Windows: a thread sleep of `ns` that returns after about ns (platform_win32.c plat_sleep_ns; the guest's
 * sceKernelDelayThread through nanosleep -> win32_nanosleep).
 *
 * A high-resolution waitable timer (Windows 10 1803+) still wakes ~0.5 ms after it was set, whatever it was asked for:
 * a guest's DelayThread(1) -- a yield in a spin-wait -- took 470-500 us. Killzone Mercenary's missions spin-wait for their
 * job threads that way, ~43 times a frame, and that alone held them near 80 fps at 120 (KZ-120, 2026-11-18). So a sleep
 * under WIN_SLEEP_SPIN_NS gives the core up (SwitchToThread) until its deadline -- at least WIN_SLEEP_MIN_NS -- on the
 * performance counter instead; a longer one waits the timer, as before. The cost: a thread in a short sleep keeps a core
 * busy for at most that sleep (1 ms).
 * runtime/tests/win_sleep_test.c times both. */
#ifndef WIN_SLEEP_H
#define WIN_SLEEP_H
#include <windows.h>
#include <stdint.h>

#define WIN_SLEEP_SPIN_NS 1000000ull   /* below 1 ms: yield to the deadline */
#define WIN_SLEEP_MIN_NS 100000ull     /* but never shorter than 100 us: a guest's lock-poll loop (lock, check, unlock, DelayThread(1))
                                        * re-took its lightweight mutex before the thread waiting for it had woken, and that one
                                        * starved -- Killzone stopped at its boot (frame 3002). 100 us is ~10x the wake latency here. */

static inline uint64_t win_sleep_now_ns(void) {
	static LARGE_INTEGER f; LARGE_INTEGER c;
	if (!f.QuadPart) QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&c);
	return (uint64_t)(c.QuadPart / f.QuadPart) * 1000000000ull + (uint64_t)(c.QuadPart % f.QuadPart) * 1000000000ull / (uint64_t)f.QuadPart;
}
static inline void win_sleep_ns(uint64_t ns) {
#ifndef WIN_SLEEP_LEGACY
	if (ns < WIN_SLEEP_SPIN_NS) {
		const uint64_t end = win_sleep_now_ns() + (ns < WIN_SLEEP_MIN_NS ? WIN_SLEEP_MIN_NS : ns);
		do SwitchToThread(); while (win_sleep_now_ns() < end);   /* (at least one yield: DelayThread(0) still gives the core up) */
		return;
	}
#endif
	static __thread HANDLE timer; static __thread int tried;
	if (!tried) { tried = 1; timer = CreateWaitableTimerExW(0, 0, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS); }   /* ~0.5 ms, not the 15.6 ms tick */
	if (timer) {
		LARGE_INTEGER due; due.QuadPart = -(LONGLONG)((ns + 99) / 100);   /* relative, 100 ns units */
		if (SetWaitableTimer(timer, &due, 0, 0, 0, FALSE)) { WaitForSingleObject(timer, INFINITE); return; }
	}
	Sleep((DWORD)((ns + 999999) / 1000000));
}
#endif
