/* cli_args_test.cpp - launcher/cli_args.h (RECOMPLAUNCHER-01 L4).
 * Build: c++ -std=c++17 -Wall -I .. cli_args_test.cpp -o /tmp/cli_args_test && /tmp/cli_args_test */
#include <cstdio>
#include <initializer_list>
#include "cli_args.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static CliArgs P(std::initializer_list<const char *> a) {
	std::vector<const char *> v{"nativ"}; v.insert(v.end(), a.begin(), a.end());
	return cli_parse((int)v.size(), v.data());
}

int main(void) {
	{ CliArgs c = P({}); CHECK(c.action == CLI_WINDOW); CHECK(c.error.empty()); CHECK(c.fullscreen == -1); CHECK(c.data_dir.empty()); }
	{ CliArgs c = P({"--help"}); CHECK(c.action == CLI_HELP); CHECK(c.error.empty()); }
	{ CliArgs c = P({"-h"}); CHECK(c.action == CLI_HELP); }
	{ CliArgs c = P({"--list"}); CHECK(c.action == CLI_LIST); CHECK(c.error.empty()); }
	{ CliArgs c = P({"--play", "PCSF00532"}); CHECK(c.action == CLI_PLAY); CHECK(c.target == "PCSF00532"); CHECK(cli_game_flags(c).empty()); }
	{ CliArgs c = P({"--play=PCSF00532"}); CHECK(c.action == CLI_PLAY); CHECK(c.target == "PCSF00532"); }
	{ CliArgs c = P({"--build", "PCSA00107"}); CHECK(c.action == CLI_BUILD); CHECK(c.target == "PCSA00107"); }
	{ CliArgs c = P({"--add", "/media/vita/ux0"}); CHECK(c.action == CLI_ADD); CHECK(c.target == "/media/vita/ux0"); }
	{ CliArgs c = P({"--data-dir", "D:/Games/Nativ", "--list"}); CHECK(c.action == CLI_LIST); CHECK(c.data_dir == "D:/Games/Nativ"); CHECK(c.error.empty()); }
	{ CliArgs c = P({"--data-dir=/x y/z"}); CHECK(c.action == CLI_WINDOW); CHECK(c.data_dir == "/x y/z"); }
	{ CliArgs c = P({"--fullscreen"}); CHECK(c.action == CLI_WINDOW); CHECK(c.fullscreen == 1); }
	{ CliArgs c = P({"--windowed"}); CHECK(c.fullscreen == 0); CHECK(c.error.empty()); }
	{ CliArgs c = P({"--fullscreen", "--windowed"}); CHECK(c.fullscreen == 0); }   /* (the last one wins) */
	/* --play with the game's own flags: --fullscreen / --windowed become the game's, then everything after -- */
	{ CliArgs c = P({"--play", "PCSF00532", "--windowed", "--", "--fullscreen", "extra word"});
	  CHECK(c.error.empty()); CHECK(c.action == CLI_PLAY);
	  std::vector<std::string> f = cli_game_flags(c);
	  CHECK(f.size() == 3); CHECK(f.size() == 3 && f[0] == "--windowed" && f[1] == "--fullscreen" && f[2] == "extra word"); }
	{ CliArgs c = P({"--play", "X", "--", "--list"}); CHECK(c.error.empty()); CHECK(c.action == CLI_PLAY); CHECK(c.game_args.size() == 1 && c.game_args[0] == "--list"); }   /* (after --: the game's, not ours) */
	{ CliArgs c = P({"-psn_0_12345", "--list"}); CHECK(c.action == CLI_LIST); CHECK(c.error.empty()); }
	/* bad command lines: an error (exit 2), never a guess */
	CHECK(!P({"--play"}).error.empty());
	CHECK(!P({"--play="}).error.empty());
	CHECK(!P({"--build"}).error.empty());
	CHECK(!P({"--add"}).error.empty());
	CHECK(!P({"--data-dir"}).error.empty());
	CHECK(!P({"--bogus"}).error.empty());
	CHECK(!P({"PCSF00532"}).error.empty());
	CHECK(!P({"--list", "--play", "X"}).error.empty());
	CHECK(!P({"--play", "X", "--build", "Y"}).error.empty());
	CHECK(!P({"--", "--fullscreen"}).error.empty());
	CHECK(!P({"--list", "--fullscreen"}).error.empty());
	CHECK(!P({"--build", "X", "--windowed"}).error.empty());
	CHECK(!P({"--list=yes"}).error.empty());
	CHECK(P({"--bogus"}).error.find("--bogus") != std::string::npos);
	CHECK(std::string(cli_usage_text()).find("--data-dir") != std::string::npos);
	printf("cli_args: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
