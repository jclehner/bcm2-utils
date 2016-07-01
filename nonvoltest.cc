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
	if (argc != 2) {
		return 1;
	}

	logger::loglevel(logger::debug);

	ifstream in(argv[1]);
	if (!in.good()) {
		return 1;
	}

	in.seekg(0x60);

	while (in.good()) {
		nv_group::sp group;
		if (nv_group::read(in, group, nv_group::type_dyn) || in.eof()) {
			cout << "** " << group->magic().to_string(false) << endl;
			print_vars(group->parts());
		}
	}

	return 0;
}
