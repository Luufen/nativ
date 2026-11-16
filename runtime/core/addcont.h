/*
 * A game's add-ons (additional content, DLC): the ones the player added are the game's, no others (DLC-01). On the
 * console an add-on is installed as ux0:addcont/<title>/<add-on>/, seen by the game as addcont0:<add-on>/, and its
 * licence is the player's: sceAppUtilDrmOpen answers for it, and refuses one that isn't installed. Here an add-on the
 * player added (`vitarecomp add <its PKG or folder>`, decrypted with its own licence) lies in the library as
 * decrypted/<title>/addcont/<add-on>/, and every run's <RECOMP_FS>/addcont0 links there -- so "installed" is that
 * folder being there, for DrmOpen and for the files a game opens to tell whether it owns an add-on (io.c's
 * RECOMP_ADDCONT_MARKERS).
 * Before, every add-on any game asked for counted as owned, bought or not.
 * The add-on's name is the part of its content id after "_00-" (EP9000-PCSF00243_00-KZBOTZONE0000001: up to 16
 * characters, letters, digits and '_' -- SceAppUtilDrmAddcontId is 17 bytes with its NUL).
 */
#ifndef ADDCONT_H
#define ADDCONT_H
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define ADDCONT_ERROR_NOT_INSTALLED 0x80100604u   /* (the console's answer to an add-on that isn't installed) */

/* a plausible add-on name: 1..16 of [A-Za-z0-9_] (no path, nothing that walks out of addcont0) */
static inline int addcont_name_ok(const char *id, size_t n) {
	if (n == 0 || n > 16) return 0;
	for (size_t i = 0; i < n; i++) {
		const char c = id[i];
		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return 0;
	}
	return 1;
}

/* is <fs_root>/addcont0/<id> (id: its first n characters) an installed add-on, a folder? */
static inline int addcont_installed(const char *fs_root, const char *id, size_t n) {
	if (!addcont_name_ok(id, n)) return 0;
	char p[1024]; struct stat st;
	if (snprintf(p, sizeof p, "%s/addcont0/%.*s", fs_root, (int)n, id) >= (int)sizeof p) return 0;
	return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* a guest path under addcont0: ("addcont0:UNIT13ONLINEPASS/unlock.dat") -> is the add-on it lies in installed? */
static inline int addcont_path_installed(const char *fs_root, const char *gp) {
	if (strncasecmp(gp, "addcont0:", 9)) return 0;
	const char *rest = gp + 9;
	while (*rest == '/') rest++;
	return addcont_installed(fs_root, rest, strcspn(rest, "/"));
}

#endif
