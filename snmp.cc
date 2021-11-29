#include "util.h"
#include "snmp.h"
using namespace std;

namespace bcm2dump {
namespace {
struct snmp_lib {
	snmp_lib()
	{ init_snmp("bcm2dump"); }
} lib;

const string cd_engr_base = "1.3.6.1.4.1.4413.2.99.1.1.3.1";
const string cd_engr_mem_addr = cd_engr_base + ".1.0";
const string cd_engr_mem_size = cd_engr_base + ".2.0";
const string cd_engr_mem_data = cd_engr_base + ".3.0";
const string cd_engr_mem_cmd  = cd_engr_base + ".4.0";

class snmp_bfc : public snmp
{
	public:
	using snmp::snmp;

	virtual bcm2_interface id() const override
	{ return BCM2_INTF_BFC; }
};

class snmp_bfc_ram : public rwx
{
	public:
	virtual limits limits_read() const override
	{ return { 4, 4, 4 }; }

	virtual limits limits_write() const override
	{ return { 4, 4, 4}; }

	virtual unsigned capabilities() const override
	{ return cap_rw; }

	protected:
	virtual std::string read_chunk(uint32_t offset, uint32_t length) override
	{
		run_mem_command(offset, length, false);
		// data is a 32-bit int that represents the data at this offset
		auto data = intf()->get(cd_engr_mem_data);
		// FIXME this shouldn't be here
		update_progress(offset, length);
		return to_buf(hton<uint32_t>(data.integer));		
	}

	virtual std::string read_special(uint32_t offset, uint32_t length) override
	{
		throw runtime_error(__func__);
	}

	virtual bool write_chunk(uint32_t offset, const std::string& chunk) override
	{
		auto value = hton(extract<uint32_t>(chunk));
		run_mem_command(offset, sizeof(value), true, value);
		// FIXME this shouldn't be here
		update_progress(offset, sizeof(value));
		return true;
	}

	private:
	void run_mem_command(uint32_t offset, uint32_t length, bool write, uint32_t value = 0)
	{
		vector<pair<string, snmp::var>> vars {
			{ cd_engr_mem_addr, { offset, ASN_UNSIGNED }},
			{ cd_engr_mem_size, { length, ASN_UNSIGNED } },
		};

		if (write) {
			vars.push_back({ cd_engr_mem_data, { value, ASN_UNSIGNED }});
		}

		vars.push_back({ cd_engr_mem_cmd, { write ? 1 : 0, ASN_INTEGER }});

		intf()->set(vars);
	}

	bcm2dump::sp<snmp> intf()
	{
		return dynamic_pointer_cast<snmp>(m_intf);
	}
};
}

snmp::snmp(string peer)
{
	snmp_session session;
	snmp_sess_init(&session);
	session.peername = &peer[0];
	session.version=SNMP_VERSION_2c;
	session.community = const_cast<u_char*>(reinterpret_cast<const u_char*>("private"));
	session.community_len = strlen(reinterpret_cast<char*>(session.community));

	m_ss = snmp_open(&session);
	if (!m_ss) {
		throw runtime_error("snmp_open");
	}
}

snmp::var snmp::get(const std::string& oid) const
{
	return get(vector<string> ({ oid })).at(0);
}

vector<snmp::var> snmp::get(const vector<string>& oids) const
{
	snmp_pdu* pdu = snmp_pdu_create(SNMP_MSG_GET);

	for (string o : oids) {
		oid oidbuf[MAX_OID_LEN];
		size_t oidlen = ARRAY_SIZE(oidbuf);

		read_objid(o.c_str(), oidbuf, &oidlen);
		snmp_add_null_var(pdu, oidbuf, oidlen);
	}

	snmp_pdu* response;
	int status = snmp_synch_response(m_ss, pdu, &response);
	cleaner c { [&response]() { snmp_free_pdu(response); }};

	if (status == STAT_SUCCESS && response->errstat == SNMP_ERR_NOERROR) {
		vector<snmp::var> ret;

		for (auto rvar = response->variables; rvar; rvar = rvar->next_variable) {
			if (rvar->type == ASN_INTEGER || rvar->type == ASN_UNSIGNED) {
				ret.push_back(var(*rvar->val.integer, rvar->type));
			} else if (rvar->type == ASN_OCTET_STR) {
				ret.push_back(var(rvar->val.string, rvar->val_len, rvar->type));
			} else {
				// FIXME
				print_variable(rvar->name, rvar->name_length, rvar);
				throw runtime_error("unhandled variable type");
			}
		}

		return ret;
	}

	throw runtime_error("snmp::get failed");
}

void snmp::set(const string& oid, const var& value)
{
	set({{ oid, value }});
}

void snmp::set(const vector<pair<string, var>>& values)
{
	snmp_pdu* pdu = snmp_pdu_create(SNMP_MSG_SET);

	for (auto p : values) {
		oid oidbuf[MAX_OID_LEN];
		size_t oidlen = ARRAY_SIZE(oidbuf);

		read_objid(p.first.c_str(), oidbuf, &oidlen);

		if (p.second.type == ASN_OCTET_STR) {
			snmp_pdu_add_variable(pdu, oidbuf, oidlen, p.second.type, p.second.str.data(), p.second.str.size());
		} else if (p.second.type == ASN_INTEGER || p.second.type == ASN_UNSIGNED) {
			auto value = p.second.integer;
			snmp_pdu_add_variable(pdu, oidbuf, oidlen, p.second.type, &value, sizeof(value));
		} else {
			throw runtime_error("unhandled variable type");
		}
	}

	snmp_pdu* response;
	int status = snmp_synch_response(m_ss, pdu, &response);
	cleaner c { [&response]() { snmp_free_pdu(response); }};

	if (status != STAT_SUCCESS) {
		throw runtime_error("snmp::set failed: status=" + to_string(status));
	} else if (response->errstat != SNMP_ERR_NOERROR) {
		throw runtime_error("snmp::set failed: "s + snmp_errstring(response->errstat));
	}
}

interface::sp snmp::detect(const string& peer)
{
	// for now
	return make_shared<snmp_bfc>(peer);
}

rwx::sp snmp::create_rwx(const addrspace& space, bool safe) const
{
	if (!space.is_ram()) {
		throw user_error("no such rwx: snmp," + space.name() + (safe ? "" : "un") + "safe");
	}

	return make_shared<snmp_bfc_ram>();
}



}
