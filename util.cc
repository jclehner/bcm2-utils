#include "profile.h"
#include "util.h"
using namespace std;

namespace bcm2dump {
namespace {
template<class T> constexpr size_t array_size(const T array)
{
	return sizeof(array) / sizeof(array[0]);
}

class profile_fixer
{
	public:
	profile_fixer()
	{
		bcm2_profile* p = bcm2_profiles;
		for (; p->name[0]; ++p) {
			bcm2_addrspace* ram = nullptr;
			for (size_t i = 0; p->spaces[i].name[0]; ++i) {
				if (p->spaces[i].name == string("ram")) {
					ram = &p->spaces[i];
				}
			}

			if (!ram) {
				throw runtime_error(string(p->name) + ": must define 'ram' address space");
			}

			ram->mem = true;

			if (p->buffer && !p->buflen && ram->size) {
				p->buflen = (ram->min + ram->size) - p->buffer;
			}

			if (ram->min & p->kseg1mask) {
				ram->min ^= p->kseg1mask;
			}
		}
	}
} profile_fixer;

}

string trim(const string& str)
{
	auto i = str.find_first_not_of(" \r\n\t");
	string ret = (i == string::npos) ? str : str.substr(i);

	i = ret.find_last_not_of(" \r\n\t");
	if (i != string::npos) {
		ret.erase(i + 1);
	}

	return ret;
}
}
