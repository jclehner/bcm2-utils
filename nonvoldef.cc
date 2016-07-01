#include "nonvol2.h"
using namespace std;

#define NV_VAR(type, name, ...) { name, make_shared<type>(__VA_ARGS__) }
#define NV_GROUP(group, ...) make_shared<group>(__VA_ARGS__)
#define NV_GROUP_DEF_CLONE(type) \
		type* clone() const \
		{ return new type(*this); }
#define NV_GROUP_DEF_CTOR_AND_CLONE(type, magic) \
		type() : nv_group(magic) {} \
		\
		NV_GROUP_DEF_CLONE(type)

namespace bcm2cfg {
namespace {

class nv_group_mlog : public nv_group
{
	public:
	NV_GROUP_DEF_CTOR_AND_CLONE(nv_group_mlog, "MLog")

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
	NV_GROUP_DEF_CTOR_AND_CLONE(nv_group_cmap, "CMAp")

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

class nv_group_8021 : public nv_group
{
	public:
	nv_group_8021(bool card2)
	: nv_group(card2 ? "8022" : "8021")
	{}

	NV_GROUP_DEF_CLONE(nv_group_8021);

	protected:
	virtual list definition(int type, int maj, int min) const
	{
		if (type != type_perm) {
			return {
				NV_VAR(nv_zstring, "ssid", 33),
				NV_VAR(nv_unknown, "data_1", 2),
				NV_VAR(nv_u8, "basic_rates"), // XXX u16?
				NV_VAR(nv_unknown, "data_2", 0x29),
				NV_VAR(nv_u16, "beacon_interval"),
				NV_VAR(nv_u16, "dtim_interval"),
				NV_VAR(nv_u16, "frag_threshold"),
				NV_VAR(nv_u16, "rts_threshold"),
				NV_VAR(nv_unknown, "data_3", 0xe8),
				NV_VAR(nv_u8, "byte_1", true),
				NV_VAR(nv_u8, "byte_2", true),
				NV_VAR(nv_u8, "byte_3", true),
				NV_VAR(nv_unknown, "data_4", 0x20),
				NV_VAR(nv_u8, "short_retry_limit"),
				NV_VAR(nv_u8, "long_retry_limit"),
				NV_VAR(nv_unknown, "data_5", 0x6),
				NV_VAR(nv_u16, "tx_power"), // XXX u8?
				NV_VAR(nv_pstring, "wpa_psk"),
				NV_VAR(nv_unknown, "data_6", 0x8),
				NV_VAR(nv_u16, "radius_port"),
				NV_VAR(nv_unknown, "data_7", 0x9d),
				NV_VAR(nv_pzstring, "wps_device_pin"),
				NV_VAR(nv_pzstring, "wps_model"),
				NV_VAR(nv_pzstring, "wps_manufacturer"),
				NV_VAR(nv_pzstring, "wps_device_name"),
				NV_VAR(nv_unknown, "data_8", 3),
				NV_VAR(nv_pzstring, "wps_model_num"),
				//NV_VAR(nv_bool, "wps_timeout"),
				NV_VAR(nv_unknown, "data_9", 2),
				NV_VAR(nv_pzstring, "wps_uuid"),
				NV_VAR(nv_pzstring, "wps_board_num"),
				NV_VAR(nv_u8, "byte_6", true),
				NV_VAR(nv_pzstring, "country"),
				NV_VAR(nv_unknown, "data_10", 0x6),
				NV_VAR(nv_u8, "pre_network_radar_check"),
				NV_VAR(nv_u8, "in_network_radar_check")
			};
		}

		return nv_group::definition(type, maj, min);
	}
};

class nv_group_rg : public nv_group
{
	public:
	NV_GROUP_DEF_CTOR_AND_CLONE(nv_group_rg, "RG..")

	protected:
	virtual list definition(int type, int maj, int min) const
	{
		return {
			NV_VAR(nv_u8, "byte_1", true),
			NV_VAR(nv_zstring, "http_pass", 9),
			NV_VAR(nv_zstring, "http_realm", 256),
			NV_VAR(nv_data, "data_1", 7),
			NV_VAR(nv_data, "data_2", 3),
			NV_VAR(nv_ip4, "dmz_ip"),
			// the next two are pure speculation
			NV_VAR(nv_zstring, "dmz_hostname", 256),
			NV_VAR(nv_mac, "dmz_mac"),
			NV_VAR(nv_data, "data_3", 7),
			NV_VAR(nv_unknown, "data_4", 0x8f6),
			NV_VAR(nv_data, "data_5", 4),
			NV_VAR(nv_u8, "timeserver_count"),
		};
	}
};

class nv_group_cdp : public nv_group
{
	public:

	NV_GROUP_DEF_CTOR_AND_CLONE(nv_group_cdp, "CDP.")

	protected:
	class nv_ip4_typed : public nv_compound
	{
		public:
		nv_ip4_typed() : nv_compound(false, 0, true) {}

		string type() const
		{ return "typed_ip"; }

		protected:
		virtual list definition() const override
		{
			return {
				NV_VAR(nv_u8, "type"),
				NV_VAR(nv_ip4, "ip")
			};
		}
	};

	class nv_lan_addr_entry : public nv_compound
	{
		public:
		nv_lan_addr_entry() : nv_compound(false, 0, true) {}

		string type() const
		{ return "lan_addr"; }

		protected:
		virtual list definition() const override
		{
			return {
				NV_VAR(nv_u16, "create_time"),
				NV_VAR(nv_data, "data_1", 2),
				NV_VAR(nv_u16, "expire_time"),
				NV_VAR(nv_u8, "ip_type"),
				NV_VAR(nv_ip4, "ip"),
				NV_VAR(nv_data, "ip_data", 3),
				NV_VAR(nv_u8, "method"),
				NV_VAR(nv_pzstring, "client_id"),
				NV_VAR(nv_pzstring, "hostname"),
				NV_VAR(nv_mac, "mac")
			};
		}

	};

	virtual list definition(int type, int maj, int min) const
	{
		return {
			NV_VAR(nv_data, "data_1", 7),
			NV_VAR(nv_u8, "lan_trans_threshold"),
			NV_VAR(nv_data, "data_2", 8),
			NV_VAR(nv_ip4_typed, "dhcp_pool_start"),
			NV_VAR(nv_ip4_typed, "dhcp_pool_end"),
			NV_VAR(nv_ip4_typed, "dhcp_subnet_mask"),
			NV_VAR(nv_data, "data_3", 5),
			NV_VAR(nv_ip4_typed, "router"),
			NV_VAR(nv_ip4_typed, "dns"),
			NV_VAR(nv_ip4_typed, "syslog"),
			NV_VAR(nv_lan_addr_entry, "lan_addr_1")
			// lan addr table format:
			// <u16 create_time><u16 ?><u16 expire_time><u8 ip_type><data[7] ip><u8 method><pzstring client_id><pzstring hostname><?mac?>...
		};
	}
};

struct registrar {
	registrar()
	{
		vector<nv_group::sp> groups = {
			NV_GROUP(nv_group_cmap),
			NV_GROUP(nv_group_mlog),
			NV_GROUP(nv_group_8021, false),
			NV_GROUP(nv_group_8021, true),
			NV_GROUP(nv_group_rg),
			NV_GROUP(nv_group_cdp)
		};

		for (auto g : groups) {
			nv_group::registry_add(g);
		}
	}
} instance;
}
}
