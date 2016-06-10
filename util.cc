#include "util.h"
using namespace std;

namespace bcm2dump {

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
