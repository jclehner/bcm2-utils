#include <iostream>
#include <string>
#include <vector>
#include "nonvol2.h"
using namespace bcm2cfg;
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
		cerr << v.name << " = ";
		if (v.val->is_set()) {
			cerr << v.val->to_string(true);
		} else {
			break;
			cerr << "<not set>";
		}
		cerr << endl;
	}
}

class nv_group_mlog : public nv_group
{
	public:
	nv_group_mlog() : nv_group("MLog", type_dyn)
	{ m_magic.parse("MLog"); }

	protected:
	virtual list definition() const
	{
#define NV_VAR(type, name, ...) { name, make_shared<type>(__VA_ARGS__) }
	return {
		NV_VAR(nv_pstring, "http_user", 32),
		NV_VAR(nv_pstring, "http_pass", 32),
		NV_VAR(nv_pstring, "http_admin_user", 32),
		NV_VAR(nv_pstring, "http_admin_pass", 32),
		NV_VAR(nv_bool, "telnet_enabled"),
		NV_VAR(nv_zstring, "remote_acc_user", 16),
		NV_VAR(nv_zstring, "remote_acc_pass", 16),
		NV_VAR(nv_u8, "telnet_ip_stacks", true),
		NV_VAR(nv_u8, "ssh_ip_stacks", true),
		NV_VAR(nv_u8, "ssh_enabled"),
		NV_VAR(nv_u8, "http_enabled"),
		NV_VAR(nv_u16, "remote_acc_timeout"),
		NV_VAR(nv_u8, "http_ipstacks", true),
		NV_VAR(nv_u8, "http_adv_ipstacks", true)
	};
#undef NV_VAR
	}
};


int main(int argc, char** argv)
{
	if (argc != 2) {
		return 1;
	}

	ifstream in(argv[1]);
	if (!in.good()) {
		return 1;
	}

	nv_group_mlog mlog;

	if (mlog.read(in) || in.eof()) {
		print_vars(mlog.parts());
		mlog.get("http_enabled")->set("beidl", "3");
		mlog.set("telnet_enabled", "false");
		print_vars(mlog.parts());
		mlog.write(cout);
	} else {
		return 1;
	}

	return 0;
}
