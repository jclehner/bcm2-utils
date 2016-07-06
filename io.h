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

#ifndef BCM2DUMP_IO_H
#define BCM2DUMP_IO_H
#include <memory>
#include <string>
#include <list>

namespace bcm2dump {

class io
{
	public:
	// end of file
	static constexpr int eof = 0x100;
	// ignore character
	static constexpr int ign = 0x101;

	typedef std::shared_ptr<io> sp;
	virtual ~io() {}

	virtual int getc() = 0;
	virtual std::string readln(unsigned timeout = 0);
	virtual std::string read(size_t length, bool partial = true) = 0;
	virtual void writeln(const std::string& buf = "") = 0;
	virtual void write(const std::string& buf) = 0;

	virtual bool pending(unsigned timeout = 100) = 0;

	static sp open_serial(const char* tty, unsigned speed);
	static sp open_telnet(const std::string& address, uint16_t port);
	static sp open_tcp(const std::string& address, uint16_t port);

	static std::list<std::string> get_last_lines();
};
}

#endif
