#include "nonvol2.h"
#include "util.h"
using namespace std;
using namespace bcm2dump;
using namespace bcm2cfg;

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0])

namespace {

class failed_test : public runtime_error
{
	public:
	failed_test(const string& msg) : runtime_error(msg) {}
};

void unserialize(const std::string& str, sp<nv_val> val)
{
	istringstream istr;
	istr.str(str);
	if (!val->read(istr)) {
		throw failed_test(val->type() + ": read error\ndata: " + to_hex(str));
	}
}

string serialize(const sp<nv_val>& val)
{
	ostringstream ostr;
	if (!val->write(ostr)) {
		throw failed_test(val->type() + " : write error\ndata: " + val->to_str());
	}

	return ostr.str();
}

void test_string_types()
{
	struct test {
		std::string data;
		std::string str;
		sp<nv_string_base> val;
		size_t bytes = string::npos;
		bool strict = true;
	} tests[] = {
		{ "\x66\x6f\x6f\x00\xde\xad"s, "foo", make_shared<nv_fstring<6>>(), 6, false },
		{ "\x66\x6f\x6f\x00\x00\x00"s, "foo", make_shared<nv_fstring<6>>(), 6 },
		{ "\x66\x6f\x6f\xde\xad"s, "foo", make_shared<nv_fstring<3>>(), 3 },

		{ "\x66\x6f\x6f\x00\xde\xad"s, "foo", make_shared<nv_fzstring<4>>(), 4 },

		{ "\x66\x6f\x6f\x00\xde\xad"s, "foo", make_shared<nv_zstring>(), 4 },

		{ "\x04\x66\x6f\x6f\x00\xde\xad"s, "foo", make_shared<nv_p8string>(), 5 },
		{ "\x03\x66\x6f\x6f\xde\xad"s, "foo", make_shared<nv_p8string>(), 4 },

		{ "\x04\x66\x6f\x6f\xde\xad"s, "foo", make_shared<nv_p8istring>(), 4 },

		{ "\x04\x66\x6f\x6f\x00\xde\xad"s, "foo", make_shared<nv_p8zstring>(), 5 },

		{ "\x00\x04\x66\x6f\x6f\x00\xde\xad"s, "foo", make_shared<nv_p16string>(), 6 },

		{ "\x00\x03\x66\x6f\x6f\xde\xad"s, "foo", make_shared<nv_p16string>(), 5 },
		{ "\x00\x05\x66\x6f\x6f\xde\xad"s, "foo", make_shared<nv_p16istring>(), 5 },
	};

	for (size_t i = 0; i < (sizeof(tests) / sizeof(tests[0])); ++i) {
		try {
			unserialize(tests[i].data, tests[i].val);
			string actual = tests[i].val->to_str();

			if (actual != tests[i].str) {
				throw failed_test(tests[i].val->type() + ": unexpected value\n"
						"expected: '" + tests[i].str + "'\n"
						"  actual: '" + actual + "'\n"
						"    data: " + to_hex(tests[i].data));
			}

			actual = serialize(tests[i].val);

			if (tests[i].bytes != string::npos && tests[i].bytes != actual.size()) {
				throw failed_test(tests[i].val->type() + ": unexpected length of serialized data\n"
						"expected: " + to_hex(tests[i].data) + " (" + to_string(tests[i].bytes) + ")\n"
						"  actual: " + to_hex(actual) + "(" + to_string(actual.size()) + ")");
			}

			if (tests[i].strict && (actual != tests[i].data.substr(0, tests[i].bytes))) {
				throw failed_test(tests[i].val->type() + ": serialized data does not match original\n"
						"expected: " + to_hex(tests[i].data) + "\n"
						"  actual: " + to_hex(actual));
			}
		} catch (const failed_test& e) {
			throw e;
		} catch (const exception& e) {
			throw failed_test(tests[i].val->type() + ": " + e.what() + "\n" +
					"data: " + to_hex(tests[i].data));
		}

		cout << "OK " << tests[i].val->type() << endl;
	}

}

template<class T> void test_int_type()
{
	typedef typename T::num_type NT;

	sp<T> val = make_shared<T>();

	for (unsigned i = 0; i != 1000; ++i) {
		NT n = bswapper<NT>::hton(rand());
		string data(reinterpret_cast<char*>(&n), sizeof(n));
		unserialize(data, val);

		if (val->num() != bswapper<NT>::ntoh(n)) {
			throw failed_test(val->type() + " " + to_string(n) + ": unexpected value " + to_string(val->num()));
		}

		string data2 = serialize(val);

		if (data2 != data) {
			throw failed_test(val->type() + " " + to_string(n) + ": serialized data does not match original\n"
					"expected: " + to_hex(data) + "\n"
					"  actual: " + to_hex(data2));
		}
	}		

	cout << "OK " << val->type() << endl;
}

void test_int_types()
{
	test_int_type<nv_u8>();
	test_int_type<nv_i8>();
	test_int_type<nv_u16>();
	test_int_type<nv_i16>();
	test_int_type<nv_u32>();
	test_int_type<nv_i32>();
	//test_int_type<nv_u64>();
	//test_int_type<nv_i64>();
}

struct enum_bitmask_test
{
	string str;
	uint16_t val;
	bool exception = false;
};

void test_enum_bitmask(nv_enum_bitmask<nv_u16>& enbm, vector<enum_bitmask_test> tests)
{
	for (enum_bitmask_test t : tests) {
		try {
			enbm.parse_checked(t.str);
		} catch (const std::exception& e) {
			if (t.exception) {
				continue;
			}

			throw failed_test(enbm.type() + ": '" + t.str + "'\n" + e.what());
		}

		if (t.exception) {
			throw failed_test(enbm.type() + " '" + t.str + "': expected exception");
		} else if (enbm.num() != t.val) {
			throw failed_test(enbm.type() + " '" + t.str + "': unexpected value " + to_string(enbm.num()));
		}
	}

	cout << "OK " << enbm.type() << endl;

}

void test_enum()
{
	nv_enum<nv_u16> en("enum", nv_enum<nv_u16>::valmap {
			{ 0, "zero" }, { 2, "two" }, { 3, "three" }});

	test_enum_bitmask(en, vector<enum_bitmask_test> {
		{ "1", 1 },
		{ "two", 2 },
		{ "three", 3 },
		{ "one", 0, true },
	});

}

void test_bitmask()
{
	nv_bitmask<nv_u16> bm("bitmask", nv_bitmask<nv_u16>::valvec {
			"bit1", "bit2", "bit3", "bit4", "bit5" });

	test_enum_bitmask(bm, {
		{ "0",     0x00 },
		{ "0x3",   0x03 },
		{ "+1",    0x03 },
		{ "+bit3", 0x07 },
		{ "+bit5", 0x17 },
		{ "-bit1", 0x16 },
		{ "-0x10", 0x06 },
		{ "+0x20", 0x26 },
		{ "-0",    0x26 },
		{ "+0",    0x26 },
		{ "+bit6", 0, true },
	});
}
}

int main()
{
	srand(time(nullptr));

	try {
		test_string_types();
		test_int_types();
		test_bitmask();
		test_enum();
	} catch (const exception& e) {
		cerr << "TEST FAILED" << endl << e.what() << endl;
		return 1;
	}

	return 0;
}
