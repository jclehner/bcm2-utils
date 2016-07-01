#include "nonvol2.h"
using namespace std;

#define NV_VAR(type, name, ...) { name, make_shared<type>(__VA_ARGS__) }

namespace bcm2cfg {
class nv_group_mlog : public nv_group
{
	public:
	nv_group_mlog() : nv_group("MLog", type_dyn) {}

	protected:
	virtual list definition(int type, int maj, int min) const
	{
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
	}
};

class nv_group_cmap : public nv_group
{
	public:
	nv_group_cmap() : nv_group("CMAp", type_dyn) {}

	protected:
	virtual list definition(int type, int maj, int min) const
	{
		return {
			NV_VAR(nv_bool, "stop_at_console"),
			NV_VAR(nv_bool, "skip_driver_init_prompt"),
			NV_VAR(nv_bool, "stop_at_console_prompt"),
			NV_VAR(nv_u8, "serial_console_mode")
		};
	}
};

namespace {
struct registrar {
	registrar()
	{
#define NV_GROUP(group) make_shared<group>()
		vector<nv_group::sp> groups = {
			NV_GROUP(nv_group_cmap),
			NV_GROUP(nv_group_mlog)
		};
#undef NV_GROUP

		for (auto g : groups) {
			nv_group::registry_add(g);
		}
	}
} instance;
}
}
