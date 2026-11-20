/* (RECOMPLAUNCHER-01 L4) the launcher's command line: every thing its window does that a script or a shortcut needs, as a
 * flag. Parsing only (no SDL, no files): hub.cpp acts on the result, launcher/tests/cli_args_test.cpp checks it.
 *
 *   nativ [--data-dir <dir>] [--fullscreen | --windowed]                 the window
 *   nativ [--data-dir <dir>] --list                                      the library, one game per line, no window
 *   nativ [--data-dir <dir>] --play <title> [--fullscreen | --windowed] [-- <game flags>...]
 *   nativ [--data-dir <dir>] --build <title>
 *   nativ [--data-dir <dir>] --add <folder>
 *   nativ --help
 * Exit status: 0 done, 1 the work failed, 2 a bad command line or a title not in the library. Unknown flags are a bad
 * command line, not ignored; "-psn_..." (an old macOS Finder's own argument) is skipped. "--opt=value" works as
 * "--opt value". Everything after "--" is the game's own (only with --play). */
#pragma once
#include <string>
#include <vector>

enum CliAction { CLI_WINDOW, CLI_HELP, CLI_LIST, CLI_PLAY, CLI_BUILD, CLI_ADD };
struct CliArgs {
	CliAction action = CLI_WINDOW;
	std::string target;            /* --play/--build: the title; --add: the folder */
	std::string data_dir;          /* --data-dir: library, settings and caches in this one folder ("" none given) */
	int fullscreen = -1;           /* --fullscreen 1, --windowed 0, -1 neither */
	std::vector<std::string> game_args;   /* after "--" (with --play: given to the game) */
	std::string error;             /* not empty: a bad command line, this says why */
};

static const char *cli_usage_text(void) {
	return
		"usage: nativ [options]\n"
		"  (no action)            open the launcher window\n"
		"  --list                 the library: one game per line, tab-separated: title, state (built | added | pack), version, name\n"
		"  --play <title>         start a game as Play does (built first when needed), no launcher window\n"
		"  --build <title>        build a game in the library, its log on stdout\n"
		"  --add <folder>         search a folder (a game, its update, add-ons, licences, firmware) and add what's ready\n"
		"  --data-dir <dir>       keep the library, settings and caches in <dir> (a portable install: a file named\n"
		"                         \"portable\" beside the launcher does the same for its own folder)\n"
		"  --fullscreen           the window (with --play: the game) fullscreen\n"
		"  --windowed             the window (with --play: the game) in a window\n"
		"  --help                 this text\n"
		"  -- <flags>             with --play: the rest is given to the game (--fullscreen, --windowed)\n"
		"exit status: 0 done, 1 the work failed, 2 a bad command line or a title not in the library\n"
		"environment (still read): ATIV_DUMP (the library), ATIV_PREFS (the settings), ATIV_TASK (play|build|scan|licences|firmware:<arg>)\n";
}

static CliArgs cli_parse(int argc, const char *const *argv) {
	CliArgs c;
	auto act = [&](CliAction a, const std::string &target) {
		if (c.action != CLI_WINDOW && c.action != a) { c.error = "only one of --list, --play, --build, --add, --help"; return; }
		c.action = a; c.target = target;
	};
	for (int i = 1; i < argc && c.error.empty(); i++) {
		std::string a = argv[i], val;
		bool has_val = false;
		if (a == "--") {
			if (c.action != CLI_PLAY) { c.error = "\"--\" (the game's own flags) needs --play <title> before it"; break; }
			for (int j = i + 1; j < argc; j++) c.game_args.push_back(argv[j]);
			break;
		}
		if (a.rfind("-psn_", 0) == 0) continue;
		const size_t eq = a.rfind("--", 0) == 0 ? a.find('=') : std::string::npos;
		if (eq != std::string::npos) { val = a.substr(eq + 1); a = a.substr(0, eq); has_val = true; }
		auto value = [&](const char *what) -> std::string {
			if (has_val) { if (val.empty()) c.error = a + " needs " + what; return val; }
			if (i + 1 >= argc || argv[i + 1][0] == '\0') { c.error = a + " needs " + what; return ""; }
			return argv[++i];
		};
		if (a == "--help" || a == "-h" || a == "-?") act(CLI_HELP, "");
		else if (a == "--list") act(CLI_LIST, "");
		else if (a == "--play") { std::string t = value("a title"); if (c.error.empty()) act(CLI_PLAY, t); }
		else if (a == "--build") { std::string t = value("a title"); if (c.error.empty()) act(CLI_BUILD, t); }
		else if (a == "--add") { std::string t = value("a folder"); if (c.error.empty()) act(CLI_ADD, t); }
		else if (a == "--data-dir") { std::string d = value("a folder"); if (c.error.empty()) c.data_dir = d; }
		else if (a == "--fullscreen") c.fullscreen = 1;
		else if (a == "--windowed") c.fullscreen = 0;
		else c.error = "unknown option " + std::string(argv[i]);
		if (c.error.empty() && has_val && (a == "--help" || a == "--list" || a == "--fullscreen" || a == "--windowed")) c.error = a + " takes no value";
	}
	if (c.error.empty() && c.fullscreen >= 0 && (c.action == CLI_LIST || c.action == CLI_BUILD || c.action == CLI_ADD))
		c.error = "--fullscreen / --windowed go with the window or --play";
	return c;
}

/* the flags --play gives the game (vitarecomp run <title> -- <these>): --fullscreen / --windowed first, then the rest */
static std::vector<std::string> cli_game_flags(const CliArgs &c) {
	std::vector<std::string> f;
	if (c.fullscreen >= 0) f.push_back(c.fullscreen ? "--fullscreen" : "--windowed");
	f.insert(f.end(), c.game_args.begin(), c.game_args.end());
	return f;
}
