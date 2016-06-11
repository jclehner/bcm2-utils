#ifndef BCM2DUMP_IO_H
#define BCM2DUMP_IO_H
#include <memory>
#include <string>
#include <list>

namespace bcm2dump {

class io
{
	public:
	typedef std::shared_ptr<io> sp;
	virtual ~io() {}

	virtual int getc() const = 0;
	virtual std::string readln(unsigned timeout = 0) const;
	virtual void writeln(const std::string& buf = "") = 0;
	virtual void write(const std::string& buf) = 0;

	virtual bool pending(unsigned timeout = 100) const = 0;

	virtual void iflush() = 0;

	static sp open_serial(const char* tty, unsigned speed);
	static std::list<std::string> get_last_lines();
};
}

#endif
