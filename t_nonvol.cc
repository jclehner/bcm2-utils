/**
 * bcm2-utils
 * Copyright (C) 2016 Joseph C. Lehner <joseph.c.lehner@gmail.com>
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

#include "nonvol2.h"
#include "nonvoldef.h"
#include "util.h"
using namespace std;
using namespace bcm2dump;
using namespace bcm2cfg;

namespace {

class failed_test : public runtime_error
{
	public:
	explicit failed_test(const string& msg) : runtime_error(msg) {}
	failed_test(const string& msg, const exception& e)
	: failed_test(msg + ":\n" + e.what()) {}
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

void test_group()
{
	class test : public nv_group
	{
		public:
		test() : nv_group("TEST", "test") {}

		virtual test* clone() const override
		{ return new test(*this); }

		virtual list definition(int type, const nv_version& ver) const override
		{
			if (ver.num() == 0) {
				return {};
			}

			return {
				NV_VAR(nv_u8, "byte"),
				NV_VAR(nv_fstring<8>, "str"),
				NV_VAR2(nv_array<nv_zstring>, "strarray", 3),
				NV_VAR(nv_p8list<nv_zstring>, "strlist"),
				NV_VAR2(nv_array<nv_u16>, "shortarray", 3),
			};
		}
	};

	nv_group::registry_add(make_shared<test>());

	string data1 =
		"\x00\x00TEST\x00\x01" // group header
		"\x5a" // u8
		"foobar\x00\x11" // fstring<8>
		"\x00\x00 bazfoo\x00" // array<zstring, 3>
		"\x00" // p8list<zstring>
		"\xf0\x0f\x55\x55\xaa\xaa"s // nv_array<nv_i16, 3>
		;

	patch(data1, 0, h_to_bes(data1.size()));

	sp<nv_group> group;
	stringstream istr(data1);

	nv_group::read(istr, group, nv_group::fmt_dyn, data1.size(), nullptr);

	if (!group) {
		istr.str(data1);
		logger::loglevel(logger::debug);
		nv_group::read(istr, group, nv_group::fmt_dyn, data1.size(), nullptr);
		logger::loglevel(logger::verbose);
		throw failed_test("failed to read group");
	}

	string data2 = serialize(group);
	if (data1 != data2) {
		throw failed_test(group->type() + ": serialized data does not match original\n"
				"expected: " + to_hex(data1) + "\n"
				"  actual: " + to_hex(data2));
	}

	struct {
		std::string name;
		std::string value;
		bool exception = false;
	} tests_set[] = {
		{ "byte", "90" },
		{ "strlist.0", "", true },
		{ "strarray.4", "", true },
		{ "barazoodox", "", true },
		{ "strlist.-1", "first" },
		{ "strlist.-1", "second" },
		{ "strlist.-1", "third" },
	};

	for (auto t : tests_set) {
		try {
			group->set(t.name, t.value);
		} catch (const exception& e) {
			if (!t.exception) {
				throw failed_test("set: " + t.name, e);
			} else {
				continue;
			}
		}
	}

	struct {
		std::string name;
		std::string value;
		bool exception = false;
	} tests_get[] = {
		{ "strlist.-1", "", true },
		{ "strlist.-1", "", true },
		{ "shortarray.3", "", true },
		{ "shmoobar", "", true },
		{ "byte.foobar", "", true },
		{ "byte", "90" },
		{ "str", "foobar" },
		{ "strarray.0", "" },
		{ "strarray.1", "" },
		{ "strarray.2", " bazfoo" },
		{ "shortarray.0", "61455" },
		{ "shortarray.2", "43690" },
	};

	for (auto t : tests_get) {
		csp<nv_val> v;

		try {
			v = group->get(t.name);
		} catch (const exception& e) {
			if (!t.exception) {
				throw failed_test("get: " + t.name, e);
			} else {
				continue;
			}
		}

		string value = v->to_str();
		if (t.exception) {
			throw failed_test(t.name + ": expected exception, got (" + v->type() + ") " + value);
		}

		if (value != t.value) {
			throw failed_test(t.name + ": unexpected value\n"
					"expected: '" + t.value + "'\n"
					"  actual: '" + value + "'");
		}
	}

	cout << "OK group" << endl;
}

void test_string_types()
{
	struct test {
		std::string data;
		std::string str;
		sp<nv_string> val;
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
		NT n = h_to_be(rand());
		string data(reinterpret_cast<char*>(&n), sizeof(n));
		unserialize(data, val);

		if (val->num() != be_to_h(n)) {
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

template<bool B> void test_enum_bitmask(nv_enum_bitmask<nv_u16, B>& enbm, vector<enum_bitmask_test> tests)
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
		test_group();
	} catch (const exception& e) {
		cerr << "TEST FAILED" << endl << e.what() << endl;
		return 1;
	}

	return 0;
}
