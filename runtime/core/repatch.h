/*
 * A game's data mods, as the Vita plugin rePatch lays them (REPATCH-01): loose files over the game's app0:, read in
 * their place. On the console rePatch takes ux0:rePatch/<title>/<path> for app0:<path> when that file is there; a
 * translation patch replaces the game's data archives that way, without touching its program. Here the player's mods
 * lie in the library as decrypted/<title>/repatch/<mod>/ (each a copy of the folder that goes to ux0:rePatch/<title>/),
 * and every run's <RECOMP_FS>/repatch links there (pipeline/vitarecomp.py link_mods), so a mod is switched on or off
 * without a build: a mod is off while decrypted/<title>/repatch/<mod>.off is there (the launcher's Mods page).
 *
 * Order: a mod's file > the update's > the game's -- the update is already layered into fs/app0 at import
 * (pipeline/packs.py layer). Several mods on: the first by name (bytewise) that has the file wins.
 * A mod never changes the game's code: Nativ's game is recompiled from the eboot.bin its pack recognises by SHA-256,
 * so eboot.bin, sce_module/ and any .self/.suprx/.elf in a mod are refused, said (the first 16 times) (the files a pipeline add
 * of a mod leaves out too). Only files are replaced or added; a folder's listing stays the game's. A mod is read only:
 * nothing is written into it (io.c's read-only check covers <RECOMP_FS>/repatch as it does app0).
 * RECOMP_REPATCH=0 runs the game without any mod.
 */
#ifndef REPATCH_H
#define REPATCH_H
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define REPATCH_MAX 32

/* a mod's name as a folder: 1..64 characters, no separator, not "." / "..", no ".off" ending (its switch's name) */
static inline int repatch_name_ok(const char *n) {
	const size_t l = strlen(n);
	if (!l || l > 64 || !strcmp(n, ".") || !strcmp(n, "..") || n[0] == '.') return 0;
	if (strpbrk(n, "/\\:")) return 0;
	return !(l >= 4 && !strcasecmp(n + l - 4, ".off"));
}

/* a path in app0 (relative, '/' separated) a mod may not replace: the game's program and its modules */
static inline int repatch_blocked(const char *rel) {
	while (*rel == '/') rel++;
	if (!strncasecmp(rel, "sce_module/", 11) || !strcasecmp(rel, "sce_module")) return 1;
	const char *base = strrchr(rel, '/'); base = base ? base + 1 : rel;
	if (!strncasecmp(base, "eboot.bin", 9)) return 1;   /* (eboot.bin, eboot.bin.elf) */
	const char *dot = strrchr(base, '.');
	return dot && (!strcasecmp(dot, ".self") || !strcasecmp(dot, ".suprx") || !strcasecmp(dot, ".elf") || !strcasecmp(dot, ".sprx"));
}

typedef struct { int n; char name[REPATCH_MAX][72]; } RepatchSet;
static int repatch_cmp(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

/* the mods on in <fs_root>/repatch: its folders with no <name>.off beside them, by name */
static inline void repatch_scan(const char *fs_root, RepatchSet *s) {
	s->n = 0;
	const char *e = getenv("RECOMP_REPATCH");
	if (e && !strcmp(e, "0")) return;
	char d[1024]; snprintf(d, sizeof d, "%s/repatch", fs_root);
	DIR *dir = opendir(d); if (!dir) return;
	struct dirent *de;
	while ((de = readdir(dir)) && s->n < REPATCH_MAX) {
		if (!repatch_name_ok(de->d_name)) continue;
		char p[1200]; struct stat st;   /* (a name is at most 64 characters: repatch_name_ok) */
		snprintf(p, sizeof p, "%.1024s/%.64s", d, de->d_name);
		if (stat(p, &st) || !S_ISDIR(st.st_mode)) continue;
		snprintf(p, sizeof p, "%.1024s/%.64s.off", d, de->d_name);
		if (!stat(p, &st)) continue;
		snprintf(s->name[s->n++], sizeof s->name[0], "%.64s", de->d_name);
	}
	closedir(dir);
	qsort(s->name, (size_t)s->n, sizeof s->name[0], repatch_cmp);
}

/* the mods, scanned once per run (repatch_reload: again, for tests) */
static RepatchSet repatch_set; static int repatch_have; static pthread_mutex_t repatch_lock = PTHREAD_MUTEX_INITIALIZER;
static inline void repatch_reload(void) { pthread_mutex_lock(&repatch_lock); repatch_have = 0; pthread_mutex_unlock(&repatch_lock); }
static inline const RepatchSet *repatch_mods(const char *fs_root) {
	pthread_mutex_lock(&repatch_lock);
	if (!repatch_have) {
		repatch_scan(fs_root, &repatch_set); repatch_have = 1;
		for (int i = 0; i < repatch_set.n; i++) fprintf(stderr, "repatch: mod \"%s\" on (its files over app0:)\n", repatch_set.name[i]);
	}
	pthread_mutex_unlock(&repatch_lock);
	return &repatch_set;
}

/* buf: a host path io.c / fios.c made of a guest path. In <fs_root>/app0/<rel> and a mod on has <rel> as a file:
 * buf becomes the mod's file. fold: the case-insensitive name match (io.c's host_path_fold; 0 for none). Returns 1
 * when redirected, 0 when not, -1 when a mod's file was refused (buf unchanged: the game's own file). */
typedef void (*repatch_fold_fn)(char *buf, size_t root_len);
static inline int repatch_apply(char *buf, size_t n, const char *fs_root, repatch_fold_fn fold) {
	const size_t rl = strlen(fs_root);
	if (strncmp(buf, fs_root, rl) || strncmp(buf + rl, "/app0/", 6)) return 0;
	const char *rel = buf + rl + 6; while (*rel == '/') rel++;
	if (!*rel) return 0;
	const RepatchSet *s = repatch_mods(fs_root);
	if (!s->n) return 0;
	const int blocked = repatch_blocked(rel);
	for (int i = 0; i < s->n; i++) {
		char p[1200]; struct stat st;
		const int w = snprintf(p, sizeof p, "%s/repatch/%s/%s", fs_root, s->name[i], rel);
		if (w < 0 || (size_t)w >= sizeof p) continue;
		if (fold) fold(p, strlen(fs_root) + 9 + strlen(s->name[i]));
		if (stat(p, &st) || !S_ISREG(st.st_mode)) continue;
		if (blocked) {
			static int said; if (__atomic_fetch_add(&said, 1, __ATOMIC_RELAXED) < 16) fprintf(stderr, "repatch: mod \"%s\" has %s: refused -- a mod can't change the game's program (eboot.bin, sce_module/, .self/.suprx/.elf); the game's own is used\n", s->name[i], rel);
			return -1;
		}
		if ((size_t)w >= n) return 0;
		memcpy(buf, p, (size_t)w + 1);
		return 1;
	}
	return 0;
}
#endif
