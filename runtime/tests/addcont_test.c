/* addcont_test.c -- which add-ons a game has (DLC-01, runtime/core/addcont.h): the ones the player added, whose folders
 * are in <RECOMP_FS>/addcont0/, no others.
 *
 * The contract: an add-on is installed when <fs>/addcont0/<name> is a folder; a name is 1..16 letters, digits or '_'
 * (anything else -- empty, too long, a path, "..": not installed, nothing outside addcont0 is looked at); a guest path
 * "addcont0:<name>/<file>" (any case of the device) is under an installed add-on when <name> is.
 * Fail-before: before DLC-01 sceAppUtilDrmOpen licensed every add-on a game asked for and addcont.h did not exist (does
 * not compile).
 *
 * Run (WSL, from this folder):
 *   cc -O1 -g -Wall -I ../core addcont_test.c -o /tmp/addcont_test && /tmp/addcont_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "addcont.h"

static int fails, checks;
static void check(const char *what, int ok) { checks++; printf("%-72s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }
static int inst(const char *root, const char *id) { return addcont_installed(root, id, strlen(id)); }

int main(void) {
	char root[] = "/tmp/addcont_testXXXXXX";
	if (!mkdtemp(root)) { perror("mkdtemp"); return 2; }
	char p[512];
	snprintf(p, sizeof p, "%s/addcont0", root); mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/addcont0/KZBOTZONE0000001", root); mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/addcont0/UNIT13ONLINEPASS", root); mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/addcont0/AFILE", root); fclose(fopen(p, "w"));   /* a file, not an add-on's folder */
	snprintf(p, sizeof p, "%s/outside", root); mkdir(p, 0755);

	check("an added add-on (its folder in addcont0) is installed", inst(root, "KZBOTZONE0000001"));
	check("another added one too", inst(root, "UNIT13ONLINEPASS"));
	check("one not added is not installed", !inst(root, "KZMAPPACK0000002"));
	check("a file of that name is not an add-on", !inst(root, "AFILE"));
	check("an empty name is not installed", !inst(root, ""));
	check("a 17-character name is not a name", !inst(root, "KZBOTZONE00000011"));
	check("a path out of addcont0 (..) is refused", !inst(root, ".."));
	check("a name with a slash is refused", !inst(root, "../outside"));
	check("only the first n characters count (the 17-byte id's)", addcont_installed(root, "KZBOTZONE0000001XYZ", 16));
	check("no addcont0 at all: nothing is installed", !inst("/nonexistent-root-for-addcont-test", "KZBOTZONE0000001"));

	check("addcont0:UNIT13ONLINEPASS/unlock.dat lies in an installed add-on", addcont_path_installed(root, "addcont0:UNIT13ONLINEPASS/unlock.dat"));
	check("ADDCONT0:/UNIT13ONLINEPASS/unlock.dat too (device case, leading slash)", addcont_path_installed(root, "ADDCONT0:/UNIT13ONLINEPASS/unlock.dat"));
	check("addcont0:OTHERPASS/unlock.dat does not", !addcont_path_installed(root, "addcont0:OTHERPASS/unlock.dat"));
	check("app0:UNIT13ONLINEPASS/unlock.dat is not an add-on path", !addcont_path_installed(root, "app0:UNIT13ONLINEPASS/unlock.dat"));
	check("addcont0:../outside/x is refused", !addcont_path_installed(root, "addcont0:../outside/x"));

	snprintf(p, sizeof p, "rm -rf '%s'", root); if (system(p)) {}
	printf(fails ? "%d of %d checks FAILED\n" : "%d checks, all passed\n", fails ? fails : checks, checks);
	return fails != 0;
}
