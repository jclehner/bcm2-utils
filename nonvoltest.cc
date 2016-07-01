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
			cerr << v.val->to_string(true);
		} else {
			break;
			cerr << "<not set>";
		}
		cerr << endl;
	}
}

int main(int argc, char** argv)
{
	if (argc != 3) {
		cerr << "usage: nonvoltest [type] [file]" << endl;
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
		nv_group::sp group;
		if (nv_group::read(in, group, type) || in.eof()) {
			if (!group) {
				break;
			}

			print_vars(group->parts());
			cout << endl;
		} else {
			return 1;
		}
	}

	return 0;
}
