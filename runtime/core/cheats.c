/*
 * The player's cheats (CHEAT-01; the owner, 2026-11-16: "füge nun auch die Möglichkeit für Cheats hinzu"): a VitaCheat
 * cheat file (.psv, cheats.h) the player chose -- the launcher keeps it as the game's cheats.psv beside its settings.ini
 * (RECOMP_CHEATS=path another) -- and the cheats switched on, applied to the game's memory once per presented frame
 * (display.c), as the VitaCheat plugin does on a console.
 * Which are on: the game's own setting cheats_on, "1,4" (their places in the file, from 1); absent: the file's _V1
 * cheats. The launcher's Cheats page and the in-game menu (Game) switch them; a switch applies from the next frame.
 * A cheat writes only the game's memory (its images and the blocks it allocated), never into its code (no effect on a
 * recompiled game: such a cheat is marked unsupported, with the reason, in the log and on the menu).
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hle.h"
#include "settings.h"
#include "cheats.h"

extern volatile uint32_t display_pad_buttons;   /* (display.c) */
static CheatSet set;
static unsigned char *on, *was_on;   /* per cheat: switched on now / applied last frame */
static int loaded;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static char path_used[1024];

static int m_ok(void *c, uint32_t a, uint32_t n) {
	(void)c;
	if (a + n < a || a < 0x10000) return 0;
	uint32_t lo[65], hi[65]; const int k = guest_mem_ranges(lo, hi, 65);
	for (int i = 0; i < k; i++) if (a >= lo[i] && a + n <= hi[i]) return 1;
	return sysmem_block_holds(a, n);
}
static int m_code(void *c, uint32_t a) { (void)c; return guest_addr_is_code(a); }
static uint32_t m_rd(void *c, uint32_t a, int b) { (void)c; uint32_t v = 0; memcpy(&v, G(a), (size_t)b); return v; }
static void m_wr(void *c, uint32_t a, int b, uint32_t v) { (void)c; memcpy(G(a), &v, (size_t)b); }
static int m_seg(void *c, int mod, int seg, uint32_t *base) { (void)c; return guest_image_segment(mod, seg, base, 0); }

static void read_on(void) {   /* cheats_on, or the file's defaults */
	char v[1024];
	if (!settings_has("cheats_on")) { for (int i = 0; i < set.nc; i++) on[i] = (unsigned char)set.c[i].def_on; return; }
	memset(on, 0, (size_t)set.nc);
	settings_str("cheats_on", "", v, sizeof v);
	for (const char *p = v; *p; ) { const long k = strtol(p, (char **)&p, 10); if (k >= 1 && k <= set.nc) on[k - 1] = 1; while (*p && (*p < '0' || *p > '9')) p++; }
}
static void load(void) {
	loaded = 1;
	const char *e = getenv("RECOMP_CHEATS");
	snprintf(path_used, sizeof path_used, "%s", e && *e ? e : "cheats.psv");
	FILE *f = fopen(path_used, "rb"); if (!f) return;
	char *text = NULL; size_t n = 0, cap = 0, r; char buf[4096];
	while ((r = fread(buf, 1, sizeof buf, f)) > 0 && n < (4u << 20)) {
		if (n + r + 1 > cap) { cap = (n + r + 1) * 2; char *t = (char *)realloc(text, cap); if (!t) break; text = t; }
		memcpy(text + n, buf, r); n += r;
	}
	fclose(f);
	if (!text) return;
	text[n] = 0;
	const int k = cheats_parse(text, &set); free(text);
	if (k <= 0) { fprintf(stderr, "    cheats: %s holds no cheats Nativ can read\n", path_used); return; }
	on = (unsigned char *)calloc((size_t)k, 1); was_on = (unsigned char *)calloc((size_t)k, 1);
	if (!on || !was_on) { cheats_free(&set); return; }
	read_on();
	int non = 0; for (int i = 0; i < k; i++) non += on[i];
	fprintf(stderr, "    cheats: %d in %s, %d on\n", k, path_used, non);
}

void cheats_frame(uint64_t frame) {
	(void)frame;
	pthread_mutex_lock(&lock);
	if (!loaded) load();
	CheatMem m = {0, m_ok, m_code, m_rd, m_wr, m_seg, display_pad_buttons};
	for (int i = 0; i < set.nc; i++) {
		if (!on[i]) { if (was_on[i]) { cheat_restore(&set, i, &m); was_on[i] = 0; } continue; }
		if (set.c[i].note[0]) continue;   /* (unsupported: tried once, said once) */
		if (cheat_apply(&set, i, &m, !was_on[i]) < 0) fprintf(stderr, "    cheats: \"%s\" unsupported: %s\n", set.c[i].name, set.c[i].note);
		else if (!was_on[i]) fprintf(stderr, "    cheats: \"%s\" on\n", set.c[i].name);
		was_on[i] = 1;
	}
	pthread_mutex_unlock(&lock);
}

/* ---- the in-game menu's (overlay.cpp) */
int cheats_count(void) { pthread_mutex_lock(&lock); if (!loaded) load(); const int n = set.nc; pthread_mutex_unlock(&lock); return n; }
void cheats_get(int i, char *name, int nn, char *note, int nnote, int *is_on) {
	pthread_mutex_lock(&lock);
	if (i >= 0 && i < set.nc) {
		snprintf(name, (size_t)nn, "%s", set.c[i].name);
		snprintf(note, (size_t)nnote, "%s", set.c[i].note);
		*is_on = on[i];
	} else { name[0] = note[0] = 0; *is_on = 0; }
	pthread_mutex_unlock(&lock);
}
void cheats_set(int i, int is_on) {   /* the setting written too (the caller saves) */
	pthread_mutex_lock(&lock);
	if (i >= 0 && i < set.nc) {
		on[i] = is_on != 0;
		char v[1024] = ""; size_t at = 0;
		for (int k = 0; k < set.nc && at + 8 < sizeof v; k++) if (on[k]) at += (size_t)snprintf(v + at, sizeof v - at, at ? ",%d" : "%d", k + 1);
		settings_set_own("cheats_on", v);
		fprintf(stderr, "    cheats: \"%s\" switched %s\n", set.c[i].name, is_on ? "on" : "off");
	}
	pthread_mutex_unlock(&lock);
}
