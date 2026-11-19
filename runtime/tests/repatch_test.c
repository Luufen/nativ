/* REPATCH-01: a game's data mods over its app0 (runtime/core/repatch.h), the order the game's file reads see:
 * a mod's file > the update's > the game's. Synthetic: a fake game (app0), its fake update (patch0), layered by the
 * pipeline's own step (pipeline/packs.py layer, as vitarecomp import does) into fs/app0, and fake mods in fs/repatch.
 * A mod's eboot.bin / sce_module / .suprx is refused (the game's own is read); a mod switched off (<mod>.off) and
 * RECOMP_REPATCH=0 read no mod; paths outside app0 are left alone.
 * Build: cc -O1 -g -Wall -I ../core repatch_test.c -lpthread   (needs python3 on PATH: the layering step) */
#include "repatch.h"
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

static char T[512];
static void put(const char *rel, const char *data) {
	char p[1024]; snprintf(p, sizeof p, "%s/%s", T, rel);
	for (char *q = p + strlen(T) + 1; *q; q++) if (*q == '/') { *q = 0; mkdir(p, 0755); *q = '/'; }
	FILE *f = fopen(p, "wb"); assert(f); fputs(data, f); fclose(f);
}
/* what the game reads at app0:<rel>: io.c's host path (<fs>/app0/<rel>), then the mods over it */
static int got_rc;
static const char *game_reads(const char *rel) {
	static char buf[1024], fs[600], out[256];
	snprintf(fs, sizeof fs, "%s/fs", T); snprintf(buf, sizeof buf, "%s/app0/%s", fs, rel);
	got_rc = repatch_apply(buf, sizeof buf, fs, 0);
	const int fd = open(buf, O_RDONLY); if (fd < 0) return "(none)";
	const ssize_t n = read(fd, out, sizeof out - 1); close(fd);
	out[n > 0 ? n : 0] = 0; return out;
}
static int fails;
static void check(const char *what, const char *rel, const char *want, int want_rc) {
	const char *g = game_reads(rel);
	const int ok = !strcmp(g, want) && got_rc == want_rc;
	printf("%s %-46s app0:%-24s -> %-8s (rc %d)\n", ok ? "ok  " : "FAIL", what, rel, g, got_rc);
	if (!ok) { printf("      want %s (rc %d)\n", want, want_rc); fails++; }
}

int main(void) {
	snprintf(T, sizeof T, "%s/repatch_test.XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp"); assert(mkdtemp(T));
	/* the game and its update; layered by the pipeline's own step */
	put("app0/eboot.bin", "GAME-EBOOT"); put("app0/sce_module/libx.suprx", "GAME-MOD");
	put("app0/data/only_game.cpk", "G1"); put("app0/data/game_upd.cpk", "G2"); put("app0/data/all3.cpk", "G3");
	put("patch0/data/game_upd.cpk", "U2"); put("patch0/data/all3.cpk", "U3"); put("patch0/data/upd_mod_off.cpk", "U4");
	char cmd[2048]; snprintf(cmd, sizeof cmd, "python3 ../../pipeline/packs.py layer %s/app0 %s/patch0 %s/fs/app0 >/dev/null", T, T, T);
	if (system(cmd)) { printf("FAIL the layering step (%s)\n", cmd); return 1; }
	/* the mods: "a-english" (on), "b-other" (on, later by name), "c-off" (switched off) */
	put("fs/repatch/a-english/data/all3.cpk", "M3"); put("fs/repatch/a-english/data/new_in_mod.cpk", "M5");
	put("fs/repatch/a-english/eboot.bin", "MOD-EBOOT"); put("fs/repatch/a-english/sce_module/libx.suprx", "MOD-MOD");
	put("fs/repatch/b-other/data/all3.cpk", "B3"); put("fs/repatch/b-other/data/only_game.cpk", "B1");
	put("fs/repatch/c-off/data/upd_mod_off.cpk", "OFF"); put("fs/repatch/c-off.off", "");

	check("game only", "data/only_game.cpk", "B1", 1);   /* (b-other has it: a mod over the game) */
	check("game + update: the update's", "data/game_upd.cpk", "U2", 0);
	check("game + update + mod: the mod's", "data/all3.cpk", "M3", 1);
	check("two mods: the first by name", "data/all3.cpk", "M3", 1);
	check("a file only the mod has", "data/new_in_mod.cpk", "M5", 1);
	check("a mod switched off: the update's", "data/upd_mod_off.cpk", "U4", 0);
	check("a mod's eboot.bin: refused, the game's", "eboot.bin", "GAME-EBOOT", -1);
	check("a mod's sce_module/*.suprx: refused", "sce_module/libx.suprx", "GAME-MOD", -1);
	check("nowhere: not found", "data/missing.cpk", "(none)", 0);
	{	/* outside app0: left alone */
		char buf[1024], fs[600]; snprintf(fs, sizeof fs, "%s/fs", T); snprintf(buf, sizeof buf, "%s/ux0/data/all3.cpk", fs);
		const int rc = repatch_apply(buf, sizeof buf, fs, 0); char want[1024]; snprintf(want, sizeof want, "%s/ux0/data/all3.cpk", fs);
		const int ok = rc == 0 && !strcmp(buf, want); printf("%s outside app0: untouched\n", ok ? "ok  " : "FAIL"); fails += !ok;
	}
	setenv("RECOMP_REPATCH", "0", 1); repatch_reload();
	check("RECOMP_REPATCH=0: no mod", "data/all3.cpk", "U3", 0);
	unsetenv("RECOMP_REPATCH"); repatch_reload();
	put("fs/repatch/a-english.off", ""); repatch_reload();   /* the launcher switches a-english off */
	check("a-english off: b-other's", "data/all3.cpk", "B3", 1);
	snprintf(cmd, sizeof cmd, "rm -rf %s", T); if (system(cmd)) {}
	if (fails) { printf("%d check(s) failed\n", fails); return 1; }
	printf("repatch_test: all checks passed (mod > update > game; a mod's program refused)\n");
	return 0;
}
