/* DECKPERF-02: the renderer's draw worker (gfx_dispatch.c, RECOMP_GFX_ASYNC). Built with gfx_dispatch.c itself and
 * stubs for the backends, which log every call with its thread. Checks: draws, scene begin/end and a scene kick run on
 * the worker in submission order; the kick's words land only after the scene's draws (not at the kick); every other
 * entry point (guest-memory hook, present) and sceGpuSignalWait's settle wait for the queue first; a nested call from
 * inside the lock does not deadlock; RECOMP_GFX_ASYNC=0 and a headless run keep it all on the caller's thread.
 * Synthetic, no game, no GPU.
 *   cc -O1 -g -Wall -D_GNU_SOURCE <INC> gfx_async_test.c -lpthread -o gfx_async_test && ./gfx_async_test */
#include "../gfx/gfx_dispatch.c"
#include <sys/wait.h>
#include <unistd.h>

/* ---- the guest arena and the stubs */
static uint8_t arena[1 << 16];
uint8_t *recomp_mem = arena;
uint32_t recomp_watch_lo = 0xffffffffu, recomp_watch_hi = 0;
void recomp_watch_write(uint32_t a, uint32_t v, int width) { (void)a; (void)v; (void)width; }

enum { EV_BEGIN = 1, EV_DRAW, EV_END, EV_PRESENT, EV_WRITING, EV_SYNC };
typedef struct { int ev; uint32_t arg; pthread_t th; uint32_t word; } Ev;   /* word: the kick's fragment word when it happened */
static Ev evs[4096]; static int nev; static pthread_mutex_t ev_m = PTHREAD_MUTEX_INITIALIZER;
#define KICK_WORD 0x100u
static void ev(int e, uint32_t arg) {
	pthread_mutex_lock(&ev_m);
	if (nev < 4096) evs[nev++] = (Ev){ e, arg, pthread_self(), RD32(KICK_WORD) };
	pthread_mutex_unlock(&ev_m);
}
static int draw_us = 200;   /* each draw's "translation": slow enough that the queue is still full at the kick */
void gfx_gpu_begin_scene(const GfxTarget *t) { ev(EV_BEGIN, t->data); }
void gfx_gpu_draw(const GfxDraw *d) { usleep((useconds_t)draw_us); ev(EV_DRAW, d->count); }
void gfx_gpu_end_scene(void) { ev(EV_END, 0); }
void gfx_gpu_present(uint32_t base, uint32_t w, uint32_t h, uint32_t p, uint32_t f) { (void)w; (void)h; (void)p; (void)f; ev(EV_PRESENT, base); }
void gfx_gpu_guest_writing(uint32_t addr, uint32_t len) { (void)len; ev(EV_WRITING, addr); }
void gfx_gpu_guest_written(uint32_t addr, uint32_t len) { (void)addr; (void)len; }
void gfx_gpu_guest_reading(uint32_t addr, uint32_t len) { (void)addr; (void)len; }
void gfx_gpu_sync_all(void) { ev(EV_SYNC, 0); gfx_guest_writing(0x200, 4); }   /* (a nested entry point, from inside the lock) */
void gfx_gpu_frame_queued(void) {}
int gfx_gpu_mv_on(void) { return 0; }
int gfx_gpu_readbacks_due(void **f, int max) { (void)f; (void)max; return 0; }
void gfx_gpu_readbacks_wait(void **f, int n) { (void)f; (void)n; }
int gfx_gpu_scene_open_on(uint32_t base) { (void)base; return 0; }
void gfx_gpu_shader_prep_tick(void) {}
int gfx_gpu_transfer_downscale(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
int gfx_gpu_vis_defer(const uint32_t *addr, const uint32_t *val, int n) { (void)addr; (void)val; (void)n; return 0; }
int gfx_gpu_vis_deferred(void) { return 0; }
void gfx_gpu_vis_flush(int wait) { (void)wait; }
void gfx_soft_begin_scene(const GfxTarget *t) { (void)t; }
void gfx_soft_draw(const GfxDraw *d) { (void)d; }
void gfx_soft_end_scene(void) {}
void gfx_soft_present(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
void gfxcap_draw(const GfxDraw *d) { (void)d; }
void gfxcap_end_scene(void) {}
void gfxcap_present(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
void gfxcap_request(void) {}
void gfxcap_scene(const GfxTarget *t) { (void)t; }
void gfxcap_target_scene(const GfxTarget *t) { (void)t; }
void gfxcov_draw(const GfxDraw *d) { (void)d; }
void gfxcov_present(uint32_t f) { (void)f; }
void gfxcov_scene(const GfxTarget *t) { (void)t; }
uint64_t display_frames(void) { return 0; }
uint64_t guest_time_us_peek(void) { return 0; }
void metrics_add_draw(void) {}
void metrics_add_lock_wait(uint64_t us) { (void)us; }
void metrics_add_render(uint64_t us) { (void)us; }
void metrics_add_scene(void) {}
void metrics_add_window_lock(uint64_t us) { (void)us; }
int metrics_costs_wanted(void) { return 0; }
int metrics_on(void) { return 0; }
void metrics_set_backend(int gpu) { (void)gpu; }
uint64_t plat_now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000; }
void plat_sleep_ns(uint64_t ns) { usleep((useconds_t)(ns / 1000)); }
int waitstats_on(void) { return 0; }
uint64_t waitstats_now(void) { return 0; }
void waitstats_begin(uint64_t t0) { (void)t0; }
void waitstats_lock(uint64_t t0) { (void)t0; }
void waitstats_held(uint64_t t0) { (void)t0; }
void waitstats_name(const char *n) { (void)n; }

static int fails;
static void check(const char *name, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", name); fails += !ok; }
static int kicked;
static void kick_done(void) { kicked++; }

/* one frame: a scene of n draws, its kick (a vertex-phase word and the fragment notification), a guest write, present */
static void frame(int n, int *kick_word_at_kick) {
	GfxTarget t; memset(&t, 0, sizeof t); t.data = 0x1000; t.width = 16; t.height = 16; t.stride = 16;
	gfx_begin_scene(&t);
	for (int i = 0; i < n; i++) { GfxDraw d; memset(&d, 0, sizeof d); d.count = (uint32_t)i; gfx_draw(&d); }
	gfx_end_scene();
	gfx_kick_done_hook = kick_done;
	const uint32_t va = 0x104, vv = 7, fa = KICK_WORD, fv = 42;
	if (!gfx_kick_async(&va, &vv, 1, &fa, &fv, 1)) { WR32(va, vv); WR32(fa, fv); kick_done(); }   /* (as gpu.c: the old way) */
	*kick_word_at_kick = (int)RD32(KICK_WORD);
	gfx_guest_writing(0x300, 4);
	gfx_present(0x1000, 16, 16, 16, 0);
}

static int mode_async(void) {   /* RECOMP_GFX_ASYNC=1 */
	const pthread_t me = pthread_self(); int at_kick = -1;
	frame(40, &at_kick);
	check("async: the kick's notification is not written at the kick (the scene's draws still queued)", at_kick == 0);
	int ok_order = nev == 1 + 40 + 1 + 1 + 1 && evs[0].ev == EV_BEGIN, worker = 1, before = 1;
	for (int i = 0; i < 40; i++) ok_order &= evs[1 + i].ev == EV_DRAW && evs[1 + i].arg == (uint32_t)i;
	ok_order &= evs[41].ev == EV_END && evs[42].ev == EV_WRITING && evs[42].arg == 0x300 && evs[43].ev == EV_PRESENT;
	for (int i = 0; i <= 41 && i < nev; i++) { worker &= !pthread_equal(evs[i].th, me); before &= evs[i].word == 0; }
	check("async: begin, 40 draws in order, end, then the guest write hook, then present", ok_order);
	check("async: begin/draws/end ran on the worker thread, not the caller", worker);
	check("async: every draw and the scene end happened before the notification landed", before);
	check("async: the hook and present ran on the caller after the queue drained", nev >= 44 && pthread_equal(evs[42].th, me) && pthread_equal(evs[43].th, me));
	check("async: kick words written (vertex 7, fragment 42), done hook once", RD32(0x104) == 7 && RD32(KICK_WORD) == 42 && kicked == 1);
	/* sceGpuSignalWait's settle: a second scene's kick, then the settle alone must land it */
	WR32(KICK_WORD, 0); nev = 0;
	GfxTarget t; memset(&t, 0, sizeof t); t.data = 0x2000; gfx_begin_scene(&t);
	for (int i = 0; i < 20; i++) { GfxDraw d; memset(&d, 0, sizeof d); d.count = 100 + (uint32_t)i; gfx_draw(&d); }
	gfx_end_scene();
	const uint32_t fa = KICK_WORD, fv = 43; gfx_kick_async(0, 0, 0, &fa, &fv, 1);
	const int pending = (int)RD32(KICK_WORD);
	gfx_async_settle();
	check("async: gfx_async_settle (sceGpuSignalWait) waits for the scene: word 0 before, 43 after", pending == 0 && RD32(KICK_WORD) == 43 && kicked == 2);
	/* a nested entry point from inside the lock (gfx_sync_to_guest -> the backend -> gfx_guest_writing) */
	nev = 0; gfx_sync_to_guest();
	check("async: a nested entry point inside the lock returns (no self-deadlock)", nev == 2 && evs[0].ev == EV_SYNC && evs[1].ev == EV_WRITING);
	return 0;
}
static int mode_sync(const char *why) {   /* RECOMP_GFX_ASYNC=0, or headless */
	const pthread_t me = pthread_self(); int at_kick = -1, caller = 1;
	frame(10, &at_kick);
	for (int i = 0; i < nev; i++) caller &= pthread_equal(evs[i].th, me);
	char n1[160], n2[160];
	snprintf(n1, sizeof n1, "%s: everything on the caller's thread", why); check(n1, caller && nev == 1 + 10 + 1 + 1 + 1);
	snprintf(n2, sizeof n2, "%s: the kick's words written at the kick", why); check(n2, at_kick == 42);
	return 0;
}
static int child(const char *async, const char *headless, int (*fn)(void)) {
	fflush(stdout);
	pid_t p = fork();
	if (p == 0) {
		if (async) setenv("RECOMP_GFX_ASYNC", async, 1); else unsetenv("RECOMP_GFX_ASYNC");
		if (headless) setenv("RECOMP_HEADLESS", headless, 1); else unsetenv("RECOMP_HEADLESS");
		fn(); fflush(stdout); _exit(fails ? 1 : 0);
	}
	int st = 0; waitpid(p, &st, 0);
	return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}
static int sync_off(void) { return mode_sync("RECOMP_GFX_ASYNC=0"); }
static int sync_headless(void) { return mode_sync("headless (RECOMP_GFX_ASYNC unset)"); }
static int async_default(void) { return mode_async(); }
int main(void) {
	int bad = 0;
	bad += child("1", "1", mode_async);      /* =1 asks for it even headless */
	bad += child(0, 0, async_default);       /* a played run: on by default */
	bad += child("0", 0, sync_off);
	bad += child(0, "1", sync_headless);
	printf(bad ? "%d mode(s) failed\n" : "gfx_async_test: all checks passed\n", bad);
	return bad ? 1 : 0;
}
