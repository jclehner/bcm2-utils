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
#include "gwsettings.h"
#include "nonvol2.h"
#include "util.h"
using namespace bcm2cfg;
using namespace bcm2dump;
using namespace std;

void read_vars(istream& is, vector<nv_val::named>& vars)
{
	size_t pos = 0;

	for (auto v : vars) {
		if (!v.val->read(is)) {
			cerr << "at pos " << pos << ": failed to parse " << v.val->type() << " (" << v.name << ")" << endl;
			break;
		}
		pos += v.val->bytes();
	}
}

void print_vars(const nv_val::list& vars)
{
	for (auto v : vars) {
		if (v.val->is_set()) {
			cerr << v.name << " = ";
			cerr << v.val->to_pretty();
		} else {
			break;
			cerr << "<not set>";
		}
		cerr << endl;
	}
}

csp<nv_val> get(sp<nv_group> group, const string& name)
{
	vector<string> tok = split(name, '.', false, 2);
	if (tok[0] == group->name()) {
		if (tok.size() == 2) {
			return group->find(tok[1]);
		} else {
			return group;
		}
	}

	return nullptr;
}

#if 0
void usage(bool help = false)
{
	ostream& os = logger::i();

	os << "Usage: bcm2cfg [<options>] <command> [<arguments> ...]\n" << endl;
	os << endl << endl;
	os << "Commands:" << endl;
	os << "  verify <infile>" << endl;
	if (help) {
		os << "\n    Verify the input file (checksum and file size).\n";
	}
	os << "  fix <infile> [<outfile>]" << endl;
	if (help) {
		os << "\n    Fixes the input file's size and checksum, optionally\n"
				"    writing the resulting file to <outfile>."
	}
	os << "  decrypt <infile> [<outfile>]" << endl;
	if (help) {
		os << "\n    Decrypts the input file, optionally writing the resulting\n"
				"    file to <outfile>. If neither key (-k) nor password (-p)\n"
				"    have been specified, but a profile is known, the default key\n"
				"    will be used (if available)."
	}
	os << "  encrypt <infile> [<outfile>]" << endl;
	if (help) {
		os << "\n    Decrypts the input file, optionally writing the resulting\n"
				"    file to <outfile>. If neither key (-k) nor password (-p)\n"
				"    have been specified, but a profile is known, the default key\n"
				"    will be used (if available)."
	}
	os << "  list <infile> [<name>]" << endl;
	if (help) {
		os << "\n    List all variable names below <name>. If omitted, dump all\n"
				"    variable names.\n";
	}
	os << "  get <infile> [<name>]" << endl;
	if (help) {
		os << "\n    Print value of variable <name>. If omitted, dump file contents.\n";
	}
	os << "  set <infile> <name> <value>" << endl;
	if (help) {
		os << "\n    Set value of variable <name> to <value>.\n";
	}

		"\n"
		"Commands:\n";
		"  ver <file>                  Verify input file\n"
		"  fix <file>                  Fix checksum and file size\n"
		"  enc <infile> [<outfile>]    Encrypt input file\n"
		"  dec <infile> [<outfile>]    Decrypt input file\n"
		"  get "

}
#endif

int main(int argc, char** argv)
{
	ios::sync_with_stdio();

	if (argc < 3) {
		cerr << "usage: nonvoltest <type> <file> {get <name>, set <name> <value>}" << endl;
		return 1;
	}

	logger::loglevel(logger::verbose);

	ifstream in(argv[2]);
	if (!in.good()) {
		cerr << "failed to open " << argv[2] << endl;
		return 1;
	}

	int type;

	if (argv[1] == "group"s && false) {
		type = nv_group::type_dyn;
	} else if (argv[1] == "dyn"s) {
		type = nv_group::type_dyn;
	} else if (argv[1] == "gws"s) {
		type = nv_group::type_dyn;
	} else if (argv[1] == "perm"s) {
		type = nv_group::type_perm;
	} else {
		cerr << "invalid type " << argv[1] << endl;
		return 1;
	}

	sp<settings> cfg = settings::read(in, type, nullptr, "");
	if (argc >= 5 && argv[3] == "get"s) {
		csp<nv_val> val = cfg->get(argv[4]);
		if (val) {
			cout << argv[4] << " = " << val->to_pretty() << endl;
		}
	} else if (argc >= 6 && argv[3] == "set"s) {
		cfg->set(argv[4], argv[5]);
		cout << argv[4] << " = " << cfg->get(argv[4])->to_pretty() << endl;
		in.close();
		ofstream out(argv[2]);
		cfg->write(out);
	} else if (argc >= 4 && argv[3] == "info"s) {
		cout << argv[2] << endl;
		cout << cfg->header_to_string() << endl;
		for (auto p : cfg->parts()) {
			csp<nv_group> g = nv_val_cast<nv_group>(p.val);
			string ugly = g->magic().to_str();
			string pretty = g->magic().to_pretty();
			cout << ugly << "  " << (ugly == pretty ? "    " : pretty) << "  ";
			string version = g->version().to_pretty();
			printf("%-6s  %-12s  %5zu b\n", version.c_str(), g->name().c_str(), g->bytes());
		}
		cout << endl;
	} else if (argc >= 5 && (argv[3] == "list"s || argv[3] == "ls"s)) {
		csp<nv_val> val = cfg->get(argv[4]);
		if (val) {
			if (!val->is_compound()) {
				cout << argv[3] << endl;
			} else {
				for (auto p : nv_compound_cast(val)->parts()) {
					cout << p.name << (p.val->is_compound() ? ".*" : "") << endl;
				}
			}
		}
	} else {
		cout << cfg->to_pretty() << endl;
	}

#if 0
	while (in.good()) {
		sp<nv_group> group;
		if (nv_group::read(in, group, type) || in.eof()) {
			if (!group || group->magic().to_str() == "ffffffff") {
				break;
			}

			if (argc == 5 && argv[3] == "get"s) {
				csp<nv_val> val = get(group, argv[4]);
				if (val) {
					cout << argv[4] << " = " << val->to_pretty() << endl;
					break;
				}
			} else if (argc == 6 && argv[3] == "set"s) {
				sp<nv_val> val = const_pointer_cast<nv_val>(get(group, argv[4]));
				if (val) {
					val->parse_checked(argv[5]);
					cout << argv[4] << " = " << val->to_pretty() << endl;
					ofstream out("grp_mod_" + group->name() + ".bin");
					group->write(out);
					break;
				}
			} else {
				cout << group->magic() << " v" << group->version() << endl;
				cout << group->to_pretty() << endl;
				cout << endl;
			}
		} else {
			return 1;
		}
	}
#endif

	return 0;
}
