#ifndef BCM2DUMP_IO_H
#define BCM2DUMP_IO_H
#include <memory>
#include <string>

namespace bcm2dump {

class io
{
	public:
	virtual ~io() {}

	virtual int getc() const = 0;
	virtual std::string readln(unsigned timeout = 0) const;
	virtual void writeln(const std::string& buf = "") = 0;
	virtual void write(const std::string& buf) = 0;

	virtual bool pending(unsigned timeout = 100) const = 0;

	virtual void iflush() = 0;

	static std::shared_ptr<io> open_serial(const char* tty, unsigned speed);

};
}

#endif
