/**
 * bcm2-utils
 * Copyright (C) 2016 Joseph Lehner <joseph.c.lehner@gmail.com>
 *
 * bcm2-utils is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * bcm2-utils is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with bcm2-utils.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <iostream>
#include <string>
#include <vector>
#include <getopt.h>
#include "gwsettings.h"
#include "nonvol2.h"
#include "util.h"
using namespace bcm2cfg;
using namespace bcm2dump;
using namespace std;

#ifndef VERSION
#define VERSION "v(unknown)"
#endif

namespace {
int usage(bool help = false)
{
	ostream& os = logger::i();

	os << "Usage: bcm2cfg [<options>] <command> [<arguments> ...]" << endl;
	os << endl;
	os << "Options: " << endl;
	os << "  -P <profile>     Force profile" << endl;
	os << "  -p <password>    Encryption password" << endl;
	os << "  -k <key>         Encryption key (hex string)" << endl;
	os << "  -f <format>      Input file format (auto/gws/dyn/perm)" << endl;
	os << "  -z               Add padding when encrypting" << endl;
	os << "  -q               Decrease verbosity" << endl;
	os << "  -v               Increase verbosity" << endl;
	os << endl;
	os << "Commands: " << endl;
	os << "  verify  <infile>" << endl;
	if (help) {
		os << "\n    Verifies the input file (checksum and file size). Returns\n"
				"    0 on success, and 2 on failed verification.\n\n";
	}
	os << "  fix     <infile> [<outfile>]" << endl;
	if (help) {
		os << "\n    Fixes the input file's size and checksum, optionally\n"
				"    writing the resulting file to <outfile>.\n\n";
	}
	os << "  decrypt <infile> [<outfile>]" << endl;
	if (help) {
		os << "\n    Decrypts the input file, optionally writing the resulting\n"
				"    file to <outfile>. If neither key (-k) nor password (-p)\n"
				"    have been specified, but a profile is known, the default key\n"
				"    will be used (if available).\n\n";
	}
	os << "  encrypt <infile> [<outfile>]" << endl;
	if (help) {
		os << "\n    Decrypts the input file, optionally writing the resulting\n"
				"    file to <outfile>. If neither key (-k) nor password (-p)\n"
				"    have been specified, but a profile is known, the default key\n"
				"    will be used (if available).\n\n";
	}
	os << "  list    <infile> [<name>]" << endl;
	if (help) {
		os << "\n    List all variable names below <name>. If omitted, dump all\n"
				"    variable names.\n\n";
	}
	os << "  get     <infile> [<name>]" << endl;
	if (help) {
		os << "\n    Print value of variable <name>. If omitted, print file contents.\n\n";
	}
	os << "  set     <infile> <name> <value> [<outfile>]" << endl;
	if (help) {
		os << "\n    Set value of variable <name> to <value>, optionally writing\n"
				"    the resulting file to <outfile>.\n\n";
	}
	os << "  dump    <infile> [<name>]" << endl;
	if (help) {
		os << "\n    Dump raw data of variable <name>. If omitted, dump file contents.\n\n";
	}
	os << "  type    <infile> <name>" << endl;
	if (help) {
		os << "\n    Display type information of variable <name>.\n\n";
	}
	os << "  info    <infile>" << endl;
	if (help) {
		os << "\n    Print general information about a config file.\n\n";
	}
	os << "  help" << endl;
	if (help) {
		os << "\n    Print this information and exit.\n\n";
	}
	os << endl;
	os <<
			"bcm2cfg " VERSION " Copyright (C) 2016 Joseph C. Lehner\n"
			"Licensed under the GNU GPLv3; source code is available at\n"
			"https://github.com/jclehner/bcm2utils\n"
			"\n";

	return help ? 0 : 1;
}

sp<settings> read_file(const string& filename, int format, const sp<profile>& profile,
		const string& key, const string& pw)
{
	ifstream infile;
	if (filename != "-") {
		infile.open(filename, ios::binary);
		if (!infile.good()) {
			throw user_error("failed to open " + filename + " for reading");
		}
	}

	istream& in = filename != "-" ? infile : cin;

	return settings::read(in, format, profile, key, pw);
}

void write_file(const string& filename, const sp<settings>& settings)
{
	ostringstream ostr;
	if (!settings->write(ostr)) {
		throw user_error("failed to serialize data");
	}

	ofstream out(filename, ios::binary);
	if (!out.good()) {
		throw user_error("failed to open " + filename + " for writing");
	}

	if (!(out << ostr.str())) {
		throw user_error("failed to write to " + filename);
	}
}

int do_list_get_dump_type(int argc, char** argv, const sp<settings>& settings)
{
	if (argc != 2 && argc != 3) {
		return usage(false);
	}

	csp<nv_val> val = (argc == 3 ? settings->get(argv[2]) : settings);

	if (argv[0] == "get"s) {
		if (argc == 3) {
			logger::i() << argv[2] << " = ";
		}

		if (logger::loglevel() < logger::info) {
			logger::i() << val->to_str() << endl;
		} else {
			logger::i() << val->to_pretty() << endl;
		}
	} else if (argv[0] == "list"s) {
		if (!val->is_compound()) {
			logger::i() << argv[2] << endl;
		} else {
			for (auto p : nv_compound_cast(val)->parts()) {
				if (p.val->is_disabled()) {
					continue;
				}

				ostream& os = (starts_with(p.name, "_unk_") || !p.val->is_set()) ? logger::v() : logger::i();
				os << (!p.val->is_set() ? "[" : "");
				if (argc == 3) {
					os << argv[2] << ".";
				}

				os << p.name << (p.val->is_compound() ? ".* " : "");
				os << (!p.val->is_set() ? "]" : "") << endl;
			}
		}
	} else if (argv[0] == "dump"s) {
		ostringstream ostr;
		if (!val->write(ostr)) {
			throw runtime_error("failed to write data");
		}

		cout << ostr.str() << flush;
	} else if (argv[0] == "type"s && argc == 3) {
		logger::i() << val->type() << endl;
		return 0;
	} else {
		return usage(false);
	}

	return 0;
}

int do_set(int argc, char** argv, const sp<settings>& settings)
{
	if (argc != 4 && argc != 5) {
		return usage(false);
	}

	settings->set(argv[2], argv[3]);
	logger::i() << argv[2] << " = " << settings->get(argv[2])->to_pretty() << endl;
	write_file(argc == 5 ? argv[4] : argv[1], settings);
	return 0;
}

int do_crypt_or_fix(int argc, char** argv, const sp<settings>& settings,
		const string& key, const string& password, bool padded)
{
	if (argc != 2 && argc != 3) {
		return usage(false);
	}

	sp<encryptable_settings> s = dynamic_pointer_cast<encryptable_settings>(settings);
	if (!s) {
		throw user_error("file format does not support encryption");
	}

	// never remove padding
	if (!s->padded()) {
		s->padded(padded);
	}

	if (argv[0] == "decrypt"s) {
		s->key("");
	} else if (argv[0] == "encrypt"s) {
		if (!s->key().empty()) {
			throw user_error("file is already encrypted");
		}

		if (key.empty()) {
			auto p = settings->profile();
			if (!p) {
				throw user_error("no profile auto-detected; use '-k <key>' or '-P <profile> -p <password>'");
			} else if (!password.empty()) {
				s->key(p->derive_key(password));
			} else {
				auto keys = p->default_keys();
				if (keys.empty()) {
					throw user_error("detected profile " + p->name() + " has no default keys; use '-k <key>' or '-p <password>'");
				}
				s->key(keys.front());
				logger::i() << "encrypted file using default key for " << p->pretty() << endl;
			}
		} else {
			s->key(from_hex(key));
		}
	} else if (argv[0] != "fix"s) {
		return usage(false);
	}

	write_file(argc == 3 ? argv[2] : argv[1], s);

	return 0;
}

int do_info(int argc, char** argv, const sp<settings>& settings)
{
	ostream& os = logger::i();

	os << argv[1] << endl;
	os << settings->header_to_string() << endl;
	for (auto p : settings->parts()) {
		csp<nv_group> g = nv_val_cast<nv_group>(p.val);
		string ugly = g->magic().to_str();
		string pretty = g->magic().to_pretty();
		os << ugly << "  " << (ugly == pretty ? "    " : pretty) << "  ";
		string version = g->is_versioned() ? g->version().to_pretty() : "";
		printf("%-6s  %-12s  %5zu b\n", version.c_str(), g->name().c_str(), g->bytes());
	}
	os << endl;

	return 0;
}

int do_verify(int argc, char** argv, const sp<settings>& settings)
{
	if (!settings->is_valid()) {
		logger::w() << "verification failed; see 'info' command for more details" << endl;
		return 2;
	}

	logger::i() << "verification successful" << endl;
	return 0;
}

int do_main(int argc, char** argv)
{
	ios::sync_with_stdio();

	int loglevel = logger::info;
	string profile_name, password, key;
	int opt = 0;
	bool pad = false;
	int format = nv_group::fmt_unknown;

	while ((opt = getopt(argc, argv, "hP:p:k:f:zvq")) != -1) {
		switch (opt) {
		case 'v':
			loglevel = max(loglevel - 1, logger::trace);
			break;
		case 'q':
			loglevel = min(loglevel + 1, logger::err);
			break;
		case 'P':
			profile_name = optarg;
			break;
		case 'p':
			password = optarg;
			key = "";
			break;
		case 'k':
			key = from_hex(optarg);
			password = "";
			break;
		case 'z':
			pad = true;
			break;
		case 'f':
			if (optarg == "gws"s) {
				format = nv_group::fmt_gws;
			} else if (optarg == "dyn"s) {
				format = nv_group::fmt_dyn;
			} else if (optarg == "perm"s) {
				format = nv_group::fmt_perm;
			} else if (optarg == "auto"s) {
				format = nv_group::fmt_unknown;
			} else {
				return usage(false);
			}
			break;
		case 'h':
		default:
			return usage(opt == 'h' || (optopt == '-' && argv[optind] == "help"s));
		}
	}

	string cmd = optind < argc ? argv[optind] : "";
	if (cmd.empty() || cmd == "help") {
		return usage(!cmd.empty());
	} else if (cmd == "dump") {
		// don't clobber the output
		logger::no_stdout();
	}

	logger::loglevel(loglevel);

	argv += optind;
	argc -= optind;

	if (argc < 2) {
		return usage(false);
	}

	sp<profile> profile = !profile_name.empty() ? profile::get(profile_name) : nullptr;
	sp<settings> settings = read_file(argv[1], format, profile, key, password);

	if (cmd == "info") {
		return do_info(argc, argv, settings);
	} else if (!settings->is_valid()) {
		throw user_error("invalid or encrypted file");
	}

	if (cmd == "encrypt" || cmd == "decrypt" || cmd == "fix") {
		return do_crypt_or_fix(argc, argv, settings, key, password, pad);
	} else if (cmd == "get" || cmd == "list" || cmd == "dump" || cmd == "type") {
		return do_list_get_dump_type(argc, argv, settings);
	} else if (cmd == "set") {
		return do_set(argc, argv, settings);
	} else if (cmd == "verify") {
		return do_verify(argc, argv, settings);
	}

	return usage(false);
}
}

int main(int argc, char** argv)
{
	try {
		return do_main(argc, argv);
	} catch (const exception& e) {
		logger::e() << "error: " << e.what() << endl;
	}

	return 1;
}
