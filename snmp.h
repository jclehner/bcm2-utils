#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include "interface.h"
#include "rwx.h"

namespace bcm2dump {

class snmp : public interface
{
	public:
	struct var
	{
		var(const std::string& str, u_char type)
		: str(str), type(type)
		{}

		var(const void* data, size_t size, u_char type = ASN_OCTET_STR)
		: str(static_cast<const char*>(data), size), type(type)
		{}

		var(long integer, u_char type)
		: integer(integer), type(type)
		{}

		std::string str;
		long integer;
		u_char type;
	};

	snmp(std::string peer);

	virtual std::string name() const override
	{ return "snmp"; }

	std::vector<var> get(const std::vector<std::string>& oids) const;
	var get(const std::string& oid) const;

	void set(const std::vector<std::pair<std::string, var>>& values);
	void set(const std::string& oid, const var& value);

	static interface::sp detect(const std::string& peer);

	virtual rwx::sp create_rwx(const addrspace& space, bool safe) const;

	protected:
	//virtual void initialize_impl() override;

	private:
	snmp_session* m_ss;
};
}
