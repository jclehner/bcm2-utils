#include <iostream>
#include <string>
#include <vector>
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

csp<nv_val> get(csp<nv_group> group, const string& name)
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

string to_pretty(csp<nv_val>& val, string name)
{
	if (val->is_compound()) {
		return val->to_pretty();
	}

	return name + " = " + val->to_pretty();
}

int main(int argc, char** argv)
{
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

	if (argv[1] == "group"s) {
		type = nv_group::type_dyn;
	} else if (argv[1] == "dyn"s) {
		type = nv_group::type_dyn;
		in.seekg(0xd2);
	} else if (argv[1] == "gwsettings"s) {
		type = nv_group::type_dyn;
		in.seekg(0x60);
	} else if (argv[1] == "perm"s) {
		type = nv_group::type_perm;
		in.seekg(0xd2);
	} else {
		cerr << "invalid type " << argv[1] << endl;
		return 1;
	}

	while (in.good()) {
		sp<nv_group> group;
		if (nv_group::read(in, group, type) || in.eof()) {
			if (!group) {
				break;
			}

			if (argc == 5 && argv[3] == "get"s) {
				csp<nv_val> val = get(group, argv[4]);
				if (val) {
					cout << to_pretty(val, argv[4]) << endl;
					//cout << argv[4] << " = " << val->to_pretty() << endl;
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

	return 0;
}
