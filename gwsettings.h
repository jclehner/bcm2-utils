#ifndef BCM2CFG_GWSETTINGS_HH
#define BCM2CFG_GWSETTINGS_HH
#include "nonvol2.h"
#include "profile.h"

namespace bcm2cfg {

// dynnv:
// 202 * \ff
// <u32 size> <u32 checksum>

class settings : public nv_compound
{
	public:
	virtual csp<bcm2dump::profile> profile() const
	{ return m_profile; }

	virtual std::string type() const override
	{ return name(); }

	virtual size_t bytes() const override = 0;
	virtual size_t data_bytes() const = 0;

	virtual const list& parts() const override
	{ return m_groups; }

	static sp<settings> read(std::istream& is, int type, const csp<bcm2dump::profile>& profile, const std::string& key);

	protected:
	settings(const std::string& name, int type, const csp<bcm2dump::profile>& p)
	: nv_compound(true, name), m_profile(p), m_type(type) {}

	virtual std::istream& read(std::istream& is) override;
	//virtual std::ostream& write(std::ostream& is) const override;

	virtual list definition() const override final
	{ throw std::runtime_error(__PRETTY_FUNCTION__); }

	csp<bcm2dump::profile> m_profile;

	private:
	int m_type;
	bool m_permissive = false;
	list m_groups;
};
}

#endif
