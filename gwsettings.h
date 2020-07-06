/**
 * bcm2-utils
 * Copyright (C) 2016 Joseph Lehner <joseph.c.lehner@gmail.com>
 *
 * bcm2-utils is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * bcm2-utils is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with bcm2-utils.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

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

	virtual std::string header_to_string() const = 0;
	virtual bool is_valid() const = 0;

	virtual void raw(bool raw)
	{ m_is_raw = raw; }

	static sp<settings> read(std::istream& is, int type, const csp<bcm2dump::profile>& profile,
			const std::string& key, const std::string& password, bool raw);

	virtual std::ostream& write(std::ostream& is) const override;

	int format() const
	{ return m_format; }

	protected:
	settings(const std::string& name, int format, const csp<bcm2dump::profile>& p)
	: nv_compound(true, name), m_profile(p), m_format(format) {}

	virtual std::istream& read(std::istream& is) override;

	virtual list definition() const override final
	{ throw std::runtime_error(__PRETTY_FUNCTION__); }

	csp<bcm2dump::profile> m_profile;

	protected:
	int m_format;

	private:
	std::string m_raw_data;
	bool m_is_raw = false;
	list m_groups;
};

class encryptable_settings : public settings
{
	public:
	virtual void key(const std::string& key) = 0;
	virtual std::string key() const = 0;
	virtual void padded(bool padded) = 0;
	virtual bool padded() const = 0;

	protected:
	using settings::settings;
};
}

#endif
