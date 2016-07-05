#include "nonvol2.h"
#include "util.h"
using namespace std;

#define NV_VAR(type, name, ...) { name, make_shared<type>(__VA_ARGS__) }
#define NV_VARN(type, name, ...) { name, nv_compound_rename(make_shared<type >(__VA_ARGS__), name) }
#define NV_VAR2(type, name, ...) { name, sp<type>(new type(__VA_ARGS__)) }
#define NV_VARN2(type, name, ...) { name, nv_compound_rename(sp<type>(new type(__VA_ARGS__)), name) }
#define NV_VAR3(cond, type, name, ...) { name, nv_val_disable<type>(shared_ptr<type>(new type(__VA_ARGS__)), !(cond)) }
#define NV_VARN3(cond, type, name, ...) { name, nv_compound_rename(nv_val_disable<type>(shared_ptr<type>(new type(__VA_ARGS__)), !(cond)), name) }

#define COMMA() ,

#define NV_GROUP(group, ...) make_shared<group>(__VA_ARGS__)
#define NV_GROUP_DEF_CLONE(type) \
		virtual type* clone() const override \
		{ return new type(*this); }
#define NV_GROUP_DEF_CTOR_AND_CLONE(type, magic, pretty) \
		type() : nv_group(magic, pretty) {} \
		\
		NV_GROUP_DEF_CLONE(type)
#define NV_COMPOUND_DEF_CTOR_AND_TYPE(ttype, tname) \
		ttype(const string& name = "") : nv_compound(false) \
		{ nv_compound::rename(name); } \
		\
		virtual string type() const override \
		{ return tname; }

namespace bcm2cfg {
namespace {

template<class T> const sp<T>& nv_val_disable(const sp<T>& val, bool disable)
{
	val->disable(disable);
	return val;
}

template<class T> const sp<T>& nv_compound_rename(const sp<T>& val, const std::string& name)
{
	val->rename(name);
	return val;
}

bool is_zero_mac(const csp<nv_mac>& mac)
{
	return mac->to_str() == "00:00:00:00:00:00";
}

class nv_timestamp : public nv_u32
{
	public:
	virtual string to_string(unsigned, bool) const override
	{
		char buf[128];
		time_t time = num();
		strftime(buf, sizeof(buf) - 1, "%F %R", localtime(&time));
		return buf;
	}
};

class nv_group_mlog : public nv_group
{
	public:
	NV_GROUP_DEF_CTOR_AND_CLONE(nv_group_mlog, "MLog", "userif");

	protected:
	virtual list definition(int type, const nv_version& ver) const override
	{
		return {
			NV_VAR(nv_p16string, "http_user", 32),
			NV_VAR(nv_p16string, "http_pass", 32),
			NV_VAR(nv_p16string, "http_admin_user", 32),
			NV_VAR(nv_p16string, "http_admin_pass", 32),
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
	NV_GROUP_DEF_CTOR_AND_CLONE(nv_group_cmap, "CMAp", "bfc")

	protected:
	virtual list definition(int type, const nv_version& ver) const override
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
	: nv_group(card2 ? "8022" : "8021", "bcmwifi"s + (card2 ? "2" : ""))
	{}

	NV_GROUP_DEF_CLONE(nv_group_8021);

	protected:
	template<size_t N> class nv_cdata : public nv_data
	{
		public:
		nv_cdata() : nv_data(N) {}
	};

	class nv_wmm : public nv_compound
	{
		public:
		NV_COMPOUND_DEF_CTOR_AND_TYPE(nv_wmm, "wmm");

		protected:
		class nv_wmm_block : public nv_compound
		{
			public:
			NV_COMPOUND_DEF_CTOR_AND_TYPE(nv_wmm_block, "wmm-block");

			protected:
			class nv_wmm_params : public nv_compound
			{
				public:
				NV_COMPOUND_DEF_CTOR_AND_TYPE(nv_wmm_params, "wmm-params");

				protected:
				virtual list definition() const override
				{
					typedef nv_u16_r<0, 15> cwminaifs;
					typedef nv_u16_r<0, 1024> cwmax;
					typedef nv_u16_r<0, 8192> txop;

					return {
						NV_VAR(cwminaifs, "cwmin"),
						NV_VAR(cwmax, "cwmax"),
						NV_VAR(cwminaifs, "aifsn"),
						NV_VAR(txop, "txop_b"),
						NV_VAR(txop, "txop_ag")
					};
				}
			};

			virtual list definition() const override
			{
				return {
					NV_VARN(nv_wmm_params, "sta"),
					NV_VARN(nv_wmm_params, "ap"),
					NV_VAR(nv_u16, "data", true),
				};
			}
		};

		virtual list definition() const override
		{
			return {
				NV_VARN(nv_wmm_block, "ac_be"),
				NV_VARN(nv_wmm_block, "ac_bk"),
				NV_VARN(nv_wmm_block, "ac_vi"),
				NV_VARN(nv_wmm_block, "ac_vo"),
			};
		}

	};

	virtual list definition(int type, const nv_version& ver) const override
	{
		typedef nv_u16_r<20, 1024> beacon_interval;
		typedef nv_u16_r<1, 255> dtim_interval;
		typedef nv_u16_r<256, 2346> frag_threshold;
		typedef nv_u16_r<1, 2347> rts_threshold;

		if (type != type_perm) {
			return {
				NV_VAR(nv_zstring, "ssid", 33),
				NV_VAR(nv_u8, "", true),
				NV_VAR(nv_u8, "channel_b"),
				NV_VAR(nv_u8, "", true),
				NV_VAR(nv_u8, "basic_rates"), // XXX u16?
				NV_VAR(nv_data, "", 0x28),
				NV_VAR(beacon_interval, "beacon_interval"),
				NV_VAR(dtim_interval, "dtim_interval"),
				NV_VAR(frag_threshold, "frag_threshold"),
				NV_VAR(rts_threshold, "rts_threshold"),
				NV_VAR(nv_data, "", 0x27),
				NV_VAR2(nv_enum<nv_u8>, "mac_policy", "mac_policy", { "disabled", "allow", "deny" }),
				NV_VARN(nv_array<nv_mac>, "mac_table", 32, &is_zero_mac),
				NV_VAR(nv_u8, "", true),
				NV_VAR(nv_u8, "", true),
				NV_VAR(nv_u8, "", true),
				NV_VAR(nv_data, "", 0x20),
				NV_VAR(nv_u8, "short_retry_limit"),
				NV_VAR(nv_u8, "long_retry_limit"),
				NV_VAR(nv_u8, "", true),
				NV_VAR(nv_u8, "channel_a"),
				NV_VAR(nv_data, "", 5),
				NV_VAR(nv_u8_m<100>, "tx_power"),
				NV_VAR(nv_p16string, "wpa_psk"),
				NV_VAR(nv_data, "", 0x8),
				NV_VAR(nv_u16, "radius_port"),
				NV_VAR(nv_u8, "", true),
				NV_VAR(nv_p8data, "radius_key"),
				NV_VAR(nv_data, "", 0x3a),
				NV_VARN(nv_wmm, "wmm"),
				//NV_VAR(nv_data, "data_7_1", 9),
				NV_VARN3(ver.num() > 0x0015, nv_compound_def, "n", "n", {
					NV_VAR(nv_u8, "bss_opmode_cap_required"),
					NV_VAR(nv_u8, "channel"),
					NV_VAR(nv_u8, "", true),
					NV_VAR(nv_u8, "bandwidth"),
					NV_VAR(nv_u8, "sideband", true),
					NV_VAR(nv_u8, "", true),
					NV_VAR(nv_u8, "", true),
				}),
				NV_VAR3(ver.num() <= 0x0015, nv_data, "", 0x33),
				NV_VAR(nv_bool, "wps_enabled"),
				NV_VAR(nv_bool, "wps_cfg_state"),
				NV_VAR(nv_p8zstring, "wps_device_pin"),
				NV_VAR(nv_p8zstring, "wps_model"),
				NV_VAR(nv_p8zstring, "wps_manufacturer"),
				NV_VAR(nv_p8zstring, "wps_device_name"),
				NV_VAR(nv_data, "", 3),
				NV_VAR(nv_p8zstring, "wps_model_num"),
				//NV_VAR(nv_bool, "wps_timeout"),
				NV_VAR(nv_data, "", 2),
				NV_VAR(nv_p8zstring, "wps_uuid"),
				NV_VAR(nv_p8zstring, "wps_board_num"),
				NV_VAR(nv_u8, "", true),
				NV_VAR(nv_p8zstring, "country"),
				NV_VAR(nv_data, "", 0x6),
				NV_VAR(nv_u8, "pre_network_radar_check"),
				NV_VAR(nv_u8, "in_network_radar_check")
			};
		}

		return nv_group::definition(type, ver);
	}
};

class nv_group_t802 : public nv_group
{
	public:
	NV_GROUP_DEF_CTOR_AND_CLONE(nv_group_t802, "T802", "tmmwifi")

	protected:
	virtual list definition(int type, const nv_version& ver) const override
	{
		return {
			NV_VAR(nv_data, "wifi_sleep", 14),
			NV_VAR(nv_fzstring<33>, "ssid_24"),
			NV_VAR(nv_fzstring<33>, "ssid_50"),
			NV_VAR(nv_u8, "", true),
			NV_VAR(nv_p8string, "wpa_psk_24"),
			NV_VAR(nv_u8, "", true),
			NV_VAR(nv_p8string, "wpa_psk_50"),
			NV_VAR(nv_data, "", 4),
			NV_VAR(nv_fzstring<33>, "wifi_opt60_replace"),
			NV_VAR(nv_data, "", 8),
			NV_VAR(nv_fstring<33>, "card1_prefix"),
			// the firmware refers to this as "Card-1 Ramdon"
			NV_VAR(nv_fzstring<33>, "card1_random"),
			NV_VAR(nv_fzstring<33>, "card2_prefix"),
			NV_VAR(nv_fzstring<33>, "card2_random"),
			NV_VAR(nv_u8, "card1_regul_rev"),
			NV_VAR(nv_u8, "card2_regul_rev"),
		};
	}
};

class nv_group_rg : public nv_group
{
	public:
	NV_GROUP_DEF_CTOR_AND_CLONE(nv_group_rg, "RG..", "rg")

	protected:
	template<int N> class nv_ip_range : public nv_compound
	{
		public:
		nv_ip_range() : nv_compound(false) {}

		virtual string type() const override
		{ return "ip" + ::to_string(N) + "_range"; }

		virtual string to_string(unsigned level, bool pretty) const override
		{
			return get("start")->to_string(level, pretty) + "," + get("end")->to_string(level, pretty);
		}


		protected:
		virtual list definition() const override
		{
			return {
				NV_VAR(nv_ip<N>, "start"),
				NV_VAR(nv_ip<N>, "end")
			};
		}
	};

	class nv_ip4_range : public nv_ip_range<4>
	{
		public:
		static bool is_end(const csp<nv_ip4_range>& r)
		{
			return r->get("start")->to_str() == "0.0.0.0" && r->get("end")->to_str() == "0.0.0.0";
		}
	};

	class nv_port_range : public nv_compound
	{
		public:
		nv_port_range() : nv_compound(false) {}

		virtual string type() const override
		{ return "port-range"; }

		static bool is_range(const csp<nv_port_range>& r, uint16_t start, uint16_t end)
		{
			return r->get_as<nv_u16>("start")->num() == start && r->get_as<nv_u16>("end")->num() == end;
		}

		protected:
		virtual list definition() const override
		{
			return {
				NV_VAR(nv_u16, "start"),
				NV_VAR(nv_u16, "end")
			};
		}

	};

	class nv_proto : public nv_enum<nv_u8>
	{
		public:
		nv_proto() : nv_enum<nv_u8>("protocol", {
			{ 0x3, "TCP" },
			{ 0x4, "UDP" },
			{ 0xfe, "TCP+UDP" }
		}) {}
	};

	class nv_port_forward : public nv_compound
	{
		public:
		NV_COMPOUND_DEF_CTOR_AND_TYPE(nv_port_forward, "port-forward");

		protected:
		virtual list definition() const override
		{
			return {
				NV_VAR(nv_ip4, "dest"),
				NV_VAR(nv_port_range, "ports"),
				NV_VAR(nv_proto, "type"),
			};
		}
	};

	class nv_port_forward_dport : public nv_compound
	{
		public:
		NV_COMPOUND_DEF_CTOR_AND_TYPE(nv_port_forward_dport, "port-forward-dport");

		protected:
		virtual list definition() const override
		{
			return {
				NV_VAR(nv_port_range, "ports"),
				NV_VAR(nv_data, "data", 4),
			};
		}
	};

	class nv_port_trigger : public nv_compound
	{
		public:
		NV_COMPOUND_DEF_CTOR_AND_TYPE(nv_port_trigger, "port-trigger");

		static bool is_end(const csp<nv_port_trigger>& pt)
		{
			return nv_port_range::is_range(pt->get_as<nv_port_range>("trigger"), 0, 0);
		}

		protected:
		virtual list definition() const override
		{
			return {
				NV_VAR(nv_port_range, "trigger"),
				NV_VAR(nv_port_range, "target"),
			};

		}
	};

	virtual list definition(int type, const nv_version& ver) const override
	{
		typedef nv_i32_r<-45000, 45000> timezone_offset;

		return {
			NV_VAR(nv_u8, "", true),
			NV_VAR(nv_zstring, "http_pass", 9),
			NV_VAR(nv_zstring, "http_realm", 256),
			NV_VAR(nv_data, "", 7),
			NV_VAR(nv_data, "", 3),
			NV_VAR(nv_ip4, "dmz_ip"),
			// the next two are pure speculation
			NV_VAR(nv_zstring, "dmz_hostname", 256),
			NV_VAR(nv_mac, "dmz_mac"),
			NV_VAR(nv_data, "", 7),
			NV_VAR(nv_data, "", 0x1ff),
			NV_VARN(nv_array<nv_ip4_range>, "ip_filters", 10 , &nv_ip4_range::is_end),
			NV_VARN(nv_array<nv_port_range>, "port_filters", 10, [] (const csp<nv_port_range>& range) {
				return nv_port_range::is_range(range, 1, 0xffff);
			}),
			NV_VARN(nv_array<nv_port_forward>, "port_forwards", 10, [] (const csp<nv_port_forward>& fwd) {
				return fwd->get("dest")->to_str() == "0.0.0.0";
			}),
			NV_VARN(nv_array<nv_mac>, "mac_filters", 10, &is_zero_mac),
			NV_VAR(nv_data, "", 0x3c),
			NV_VARN(nv_array<nv_port_trigger>, "port_triggers", 10, &nv_port_trigger::is_end),
			NV_VAR(nv_data, "", 0x15),
			NV_VARN(nv_array<nv_proto>, "port_filter_protocols", 10),
			NV_VAR(nv_data, "", 0xaa),
			NV_VARN(nv_array<nv_proto>, "port_trigger_protocols", 10),
			NV_VAR(nv_data, "", 0x48a),
			NV_VAR(nv_data, "", 4),
			NV_VARN(nv_p8list<nv_p8string>, "timeservers"),
			NV_VAR(timezone_offset, "timezone_offset"),
			NV_VAR(nv_data, "", 0x25),
			NV_VARN(nv_array<nv_port_forward_dport>, "port_forward_dports", 10, [] (const csp<nv_port_forward_dport>& range) {
				return nv_port_range::is_range(range->get_as<nv_port_range>("ports"), 0, 0);
			}),
			NV_VAR(nv_data, "", 0x9b),

		};
	}
};

class nv_group_cdp : public nv_group
{
	public:

	NV_GROUP_DEF_CTOR_AND_CLONE(nv_group_cdp, "CDP.", "dhcp")

	protected:
	class nv_ip4_typed : public nv_compound
	{
		public:
		nv_ip4_typed() : nv_compound(false) {}

		virtual string type() const override
		{ return "typed_ip"; }

		protected:
		virtual list definition() const override
		{
			return {
				NV_VAR(nv_u32, "type"),
				NV_VAR(nv_ip4, "ip")
			};
		}
	};

	class nv_lan_addr_entry : public nv_compound
	{
		public:
		nv_lan_addr_entry() : nv_compound(false) {}

		string type() const override
		{ return "lan_addr"; }

		protected:
		virtual list definition() const override
		{
			return {
				NV_VAR(nv_u16, "num_1"),
				NV_VAR(nv_u16, "create_time"),
				NV_VAR(nv_u16, "num_2"),
				NV_VAR(nv_u16, "expire_time"),
				NV_VAR(nv_u8, "ip_type"),
				NV_VAR(nv_ip4, "ip"),
				NV_VAR(nv_data, "ip_data", 3),
				NV_VAR(nv_u8, "method"),
				NV_VAR(nv_p8data, "client_id"),
				NV_VAR(nv_p8string, "hostname"),
				NV_VAR(nv_mac, "mac"),
			};
		}
	};

	virtual list definition(int type, const nv_version& ver) const override
	{
		return {
			NV_VAR(nv_data, "", 7),
			NV_VAR(nv_u8, "lan_trans_threshold"),
			NV_VAR(nv_data, "", 8),
			NV_VARN(nv_ip4_typed, "dhcp_pool_start"),
			NV_VARN(nv_ip4_typed, "dhcp_pool_end"),
			NV_VARN(nv_ip4_typed, "dhcp_subnet_mask"),
			NV_VAR(nv_data, "", 4),
			NV_VARN(nv_ip4_typed, "router"),
			NV_VARN(nv_ip4_typed, "dns"),
			NV_VARN(nv_ip4_typed, "syslog"),
			NV_VAR(nv_u32, "ttl"),
			NV_VAR(nv_data, "", 4),
			NV_VARN(nv_ip4_typed, "ip_2"),
			NV_VAR(nv_p8string, "domain"),
			NV_VAR(nv_data, "", 7),
			//NV_VARN(nv_ip4_typed, "ip_3"),
			NV_VARN(nv_array<nv_lan_addr_entry>, "lan_addrs", 16),
			//NV_VAR(nv_lan_addr_entry, "lan_addr_1"), // XXX make this an array
		};
	}
};


class nv_group_fire : public nv_group
{
	public:
	NV_GROUP_DEF_CTOR_AND_CLONE(nv_group_fire, "FIRE", "firewall")

	protected:
	virtual list definition(int type, const nv_version& ver) const override
	{
		return {
			NV_VAR(nv_data, "", 2),
			NV_VAR2(nv_bitmask<nv_u16>, "features", nv_bitmask<nv_u16>::valvec {
				"url_keyword_blocking",
				"url_domain_blocking",
				"http_proxy_blocking",
				"disable_cookies",
				"disable_java_applets",
				"disable_activex_ctrl",
				"disable_popups",
				"mac_tod_filtering",
				"email_alerts",
				"",
				"",
				"",
				""
				"block_fragmented_ip",
				"port_scan_detection",
				"syn_flood_detection"
			}),
		};
	}
};

class nv_group_cmev : public nv_group
{
	public:
	NV_GROUP_DEF_CTOR_AND_CLONE(nv_group_cmev, "CMEV", "cmlog")

	protected:
	class nv_log_entry : public nv_compound
	{
		public:
		NV_COMPOUND_DEF_CTOR_AND_TYPE(nv_log_entry, "log-entry")

		static bool is_end(const csp<nv_log_entry>& log_entry)
		{
			return log_entry->get("msg")->bytes() == 0;
		}

		virtual list definition() const override
		{
			return {
				NV_VAR(nv_data, "data", 0x10),
				NV_VAR(nv_p16string, "msg")
			};
		}
	};


	virtual list definition(int type, const nv_version& ver) const override
	{
		return {
			NV_VAR(nv_u8, "", true), // maybe a p16list??
			NV_VARN(nv_p8list<nv_log_entry>, "log"),
		};
	}
};

class nv_group_xxxl : public nv_group
{
	public:
	nv_group_xxxl(const string& magic)
	: nv_group(magic, bcm2dump::transform(magic, ::tolower))
	{}

	NV_GROUP_DEF_CLONE(nv_group_xxxl)

	protected:
	class nv_log_entry : public nv_compound
	{
		public:
		NV_COMPOUND_DEF_CTOR_AND_TYPE(nv_log_entry, "log-entry")

		protected:
		virtual list definition() const override
		{
			return {
				NV_VAR(nv_timestamp, "time"),
				NV_VAR(nv_p16istring, "msg"),
				NV_VAR(nv_data, "", 2)
			};
		}
	};

	virtual list definition(int type, const nv_version& ver) const override
	{
		return {
			NV_VAR(nv_data, "", 1),
			NV_VARN(nv_p8list<nv_log_entry>, "log")
		};
	}
};

class nv_group_upc : public nv_group
{
	public:
	NV_GROUP_DEF_CTOR_AND_CLONE(nv_group_upc, "UPC.", "upc")

	virtual list definition(int type, const nv_version& ver) const override
	{
		return {
			NV_VAR(nv_data, "", 10),
			NV_VAR(nv_u16, "parental_activity_time_enable"),
			NV_VAR(nv_zstring, "parental_password", 10),
			NV_VAR(nv_data, "", 0x2237),
			NV_VAR(nv_u8, "web_country"),
			NV_VAR(nv_u8, "web_language"),
			NV_VAR(nv_bool, "web_syslog_enable"),
			NV_VAR(nv_u8, "web_syslog_level"),
			NV_VAR2(nv_array<nv_mac>, "trusted_macs", 10, &is_zero_mac),
			NV_VAR(nv_data, "", 0xd8),
			NV_VAR(nv_array<nv_ip4>, "lan_dns4_list", 3),
			NV_VAR(nv_array<nv_ip6>, "lan_dns6_list", 3),
		};
	}
};

struct registrar {
	registrar()
	{
		vector<sp<nv_group>> groups = {
			NV_GROUP(nv_group_cmap),
			NV_GROUP(nv_group_mlog),
			NV_GROUP(nv_group_8021, false),
			NV_GROUP(nv_group_8021, true),
			NV_GROUP(nv_group_t802),
			NV_GROUP(nv_group_rg),
			NV_GROUP(nv_group_cdp),
			NV_GROUP(nv_group_fire),
			NV_GROUP(nv_group_cmev),
			NV_GROUP(nv_group_upc),
			NV_GROUP(nv_group_xxxl, "RSTL"),
			NV_GROUP(nv_group_xxxl, "CMBL"),
			NV_GROUP(nv_group_xxxl, "EMBL")
		};

		for (auto g : groups) {
			nv_group::registry_add(g);
		}
	}
} instance;
}
}
