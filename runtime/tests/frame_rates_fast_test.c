/*
 * frame_rates_fast_test.c -- runtime/ui/frame_rates.h's rates at which a game runs faster than normal (a pack's RUN_ENV
 * RECOMP_FRAME_RATES_FAST, owner 2026-11-18: LittleBigPlanet keeps 120 with a hint that it runs faster): the parsing,
 * the per-rate test and the words the menu and the launcher put into their hint.
 *
 * -DFRAME_RATES_FAST_BEFORE builds the same checks against the header before the marker (no fast rates anywhere: no
 * hint) -- the fail-before.
 *
 *   cc -O1 -Wall -I ../ui frame_rates_fast_test.c -o /tmp/frame_rates_fast_test && /tmp/frame_rates_fast_test
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "frame_rates.h"
#ifdef FRAME_RATES_FAST_BEFORE
#define fr_fast(l, a) ((void)(l), (void)(a), 0)
#define fr_is_fast(r, f) ((void)(r), (void)(f), 0)
#define fr_setting_for(v, o, f) ((void)(o), (void)(f), (v))
static int words_before(int f, const char *a, char *o, int c) { (void)f; (void)a; (void)c; o[0] = 0; return 0; }
#define fr_fast_words words_before
#endif

static int checks, failed;
#define CHECK(c) do { checks++; if (!(c)) { failed++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

int main(void) {
	const int all = fr_allowed("30,60,120"), lo = fr_allowed("30,60");
	/* parsing */
	CHECK(fr_fast(NULL, all) == 0);                         /* no marker: no hint (every other game) */
	CHECK(fr_fast("", all) == 0);
	CHECK(fr_fast("60,120", all) == (FR_60 | FR_120));      /* LittleBigPlanet */
	CHECK(fr_fast("120", all) == FR_120);
	CHECK(fr_fast(" 60 , 120 ", all) == (FR_60 | FR_120));  /* spaces */
	CHECK(fr_fast("30,60", all) == FR_60);                  /* 30 is the game's own speed: never marked */
	CHECK(fr_fast("60,90,45", all) == FR_60);              /* other numbers ignored */
	CHECK(fr_fast("60,120", lo) == FR_60);                  /* a rate the game doesn't allow isn't shown, so no hint */
	CHECK(fr_fast("60,120", fr_allowed("30")) == 0);
	/* per rate */
	const int f = fr_fast("60,120", all);
	CHECK(!fr_is_fast(30, f));
	CHECK(fr_is_fast(60, f));
	CHECK(fr_is_fast(120, f));
	CHECK(!fr_is_fast(120, fr_fast("60", all)));
	CHECK(!fr_is_fast(60, 0));
	/* the setting used: in a game with fast rates only its own choice counts (the default stays 30) */
	CHECK(fr_setting_for(60, 0, f) == 30);                   /* the shared recommended 60: not taken */
	CHECK(fr_setting_for(120, 0, f) == 30);                  /* another game's shared 120: not taken */
	CHECK(fr_setting_for(120, 1, f) == 120);                 /* chosen for this game: kept */
	CHECK(fr_setting_for(30, 1, f) == 30);
	CHECK(fr_setting_for(60, 0, 0) == 60);                   /* a game without fast rates: the shared value as before */
	CHECK(fr_effective(fr_setting_for(60, 0, f), all) == 30);
	/* hint words */
	char b[32];
	CHECK(fr_fast_words(f, "and", b, sizeof b) == 2 && !strcmp(b, "60 and 120"));
	CHECK(fr_fast_words(f, "und", b, sizeof b) == 2 && !strcmp(b, "60 und 120"));
	CHECK(fr_fast_words(FR_120, "and", b, sizeof b) == 1 && !strcmp(b, "120"));
	CHECK(fr_fast_words(FR_60, "and", b, sizeof b) == 1 && !strcmp(b, "60"));
	CHECK(fr_fast_words(0, "and", b, sizeof b) == 0 && b[0] == 0);
	char t[6]; fr_fast_words(f, "and", t, sizeof t);        /* a short buffer: cut, still terminated */
	CHECK(strlen(t) == 5 && !strcmp(t, "60 an"));
	printf("%d checks, %d failed\n", checks, failed);
	return failed != 0;
}
