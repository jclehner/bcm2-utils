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

#ifndef BCM2UTILS_PROFILE_H
#define BCM2UTILS_PROFILE_H
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "asmdef.h"

#define BCM2_PATCH_NUM 4
#define BCM2_INTF_NUM 2

#ifdef __cplusplus
#include <string>
#include <vector>
#include <memory>
#include <map>

extern "C" {
#endif

enum bcm2_cfg_flags
{
	BCM2_CFG_ENC_MASK = 0x0f,
	BCM2_CFG_ENC_NONE = 0,
	BCM2_CFG_ENC_AES256_ECB = 1,
	BCM2_CFG_ENC_3DES_ECB = 2,
	BCM2_CFG_ENC_MOTOROLA = 3,
	BCM2_CFG_ENC_SUB_16x16 = 4,
	BCM2_CFG_ENC_XOR = 5,
	BCM2_CFG_ENC_DES_ECB = 6,
	BCM2_CFG_ENC_AES128_CBC = 7,

	BCM2_CFG_PAD_MASK = 0xf0,
	BCM2_CFG_PAD_ANSI_X9_23 = 1 << 4,
	BCM2_CFG_PAD_PKCS7 = 2 << 4,
	BCM2_CFG_PAD_ZEROBLK = 3 << 4,
	// similar to ANSI X9.23, but the last byte
	// is the padding length, minus one. assuming
	// a block size of 8 bytes, and data length of
	// 9 bytes, the 7 padding bytes would be
	//
	// 00 00 00 00 00 00 06
	//
	BCM2_CFG_PAD_ANSI_ISH = 4 << 4,
	BCM2_CFG_PAD_ZERO = 5 << 4,

	BCM2_CFG_FMT_MASK = 0xf00,
	BCM2_CFG_FMT_GWS_DYNNV = 1 << 8,
	BCM2_CFG_FMT_GWS_FULL_ENC = 1 << 9,
	BCM2_CFG_FMT_GWS_PAD_OPTIONAL = 1 << 10,
	// prepend data length
	BCM2_CFG_FMT_GWS_LEN_PREFIX = 1 << 11,
	BCM2_CFG_FMT_GWS_CLEN_PREFIX = 1 << 12,

	BCM2_CFG_DATA_MASK = 0xf0000,
	BCM2_CFG_DATA_USERIF_ALT = 1 << 16,
	BCM2_CFG_DATA_USERIF_ALT_SHORT = 1 << 17,
};

enum bcm2_interface
{
	BCM2_INTF_NONE = 0,
	BCM2_INTF_ALL = ~0,
	BCM2_INTF_BLDR = 2,
	BCM2_INTF_BFC = 3
};

enum bcm2_mem
{
	BCM2_MEM_NONE = 0,
	BCM2_MEM_R = 1,
	BCM2_MEM_RW = 2
};

enum bcm2_arch
{
	BCM2_UNKNOWN = 0,
	BCM2_3345 = 0x3345,
	BCM2_3368 = 0x3368,
	BCM2_3380 = 0x3380,
	BCM2_3382 = 0x3382,
	BCM2_3383 = 0x3383,
	BCM2_3384 = 0x3384,
	BCM2_3390 = 0x3390,
};

struct bcm2_partition {
	// partition name
	char name[32];
	// offset (absolute, not relative
	// to address space begin)
	uint32_t offset;
	// size
	uint32_t size;
	// internal partition name (for BFC `/flash/open`)
	char altname[32];
};

struct bcm2_patch {
	// patch this address...
	uint32_t addr;
	// ... with this word
	uint32_t word;
} __attribute__((packed));

struct bcm2_func {
	// address of this function
	uint32_t addr;
	// mode of this function. interpretation
	// depends on actual function.
	uint32_t mode;
	// patches to be applied to the bootloader
	// before using this function.
	struct bcm2_patch patch[BCM2_PATCH_NUM];
	// return value type
	int retv;
	// interface(s) this function is valid for
	int intf;
};

struct bcm2_addrspace {
	// short name to identify the address space. if a
	// device has only one flash chip, name it "flash".
	// if it has an spi/nand combo, name the spi device
	// "nvram", and the nand "flash". always define at
	// least one address space called "ram".
	char name[16];
	// used for memory mapped address space. ignored
	// for address spaces named "ram".
	enum bcm2_mem mem;
	// combination of interface ids from bcm2_interface;
	// 0 means all interfaces supported
	int intf;
	// minimum offset of this address space
	uint32_t min;
	// size of this address space. can be 0 to disable size
	// check
	uint32_t size;
	// eraseblock size, for flash
	uint32_t blocksize;
	// 0 = automatic (4 for memory, 1 for everything else)
	unsigned alignment;
	// partitions within this address space
	struct bcm2_partition parts[32];
	// read functions to read from this address space (can
	// be left blank for ram segment)
	struct bcm2_func read[BCM2_INTF_NUM];
	// not yet used
	struct bcm2_func write[BCM2_INTF_NUM];
	// not yet used
	struct bcm2_func erase[BCM2_INTF_NUM];
};

struct bcm2_magic {
	uint32_t addr;
	char data[32];
	uint8_t size;
};

struct bcm2_version_addrspace
{
	char name[16];
	struct bcm2_func open;
	struct bcm2_func read;
	struct bcm2_func write;
	struct bcm2_func erase;
	struct bcm2_func close;
};

enum bcm2_type
{
	BCM2_TYPE_U32 = 0,
	BCM2_TYPE_STR = 1,
	BCM2_TYPE_BOOL = BCM2_TYPE_U32,
	BCM2_TYPE_NIL = 9,
};

struct bcm2_typed_val
{
	char name[32];
	union {
		uint32_t n;
		char s[32];
	} val;
	enum bcm2_type type;
};

#define BCM2_VAL_U32(name, val) BCM2_TYPED_VAL(name, n, val, BCM2_TYPE_U32)
#define BCM2_VAL_STR(name, str) BCM2_TYPED_VAL(name, s, str, BCM2_TYPE_STR)

#define BCM2_TYPED_VAL(a_name, a_dest, a_val, a_type) \
	{ .name = (a_name), .val = { .a_dest = (a_val) }, .type = a_type }

struct bcm2_version {
	char version[16];
	int intf;
	struct bcm2_magic magic;
	// address where dump code can be loaded (dump code
	// is short, currently around 512 bytes)
	uint32_t rwcode;
	// location in memory where we can store read images
	uint32_t buffer;
	// length of buffer (if 0, buffer will be checked 
	// against "ram" address space)
	uint32_t buflen;
	// address of a function that behaves like printf:
	// printf a0 = format string, a1...aX format args
	uint32_t printf;
	// address of a function that behaves like scanf:
	// a0 = format, a1...aX = args
	uint32_t scanf;
	// address of a sscanf-like function
	// a0 = str, a1 = format, a2...aX = args
	uint32_t sscanf;
	// address of a getline-like function, minus the stream:
	// a0 = buffer, a1 = size
	uint32_t getline;
	struct bcm2_version_addrspace spaces[8];
	struct bcm2_typed_val options[8];
};

struct bcm2_profile {
	// short name that is used to select a profile
	char name[16];
	// pretty device name
	char pretty[64];
	// little endian MIPS (not supported at the moment)
	bool mipsel;
	// architecture
	enum bcm2_arch arch;
	// signature for ProgramStore images
	uint16_t pssig;
	// signature for compressed bootloader images
	uint16_t blsig;
	// baudrate of the bootloader console
	uint32_t baudrate;
	// address mask for uncached mips segment
	uint32_t kseg1mask;
	// a location in memory with a constant value (ideally a
	// bootloader string), which can be used to automatically
	// identify the connected device
	struct bcm2_magic magic[BCM2_INTF_NUM];
	// settings regarding the config dump format
	uint32_t cfg_flags;
	// a key that is appended to the configuration file data
	// before calculating its checksum. specify as a hex string 
	char cfg_md5key[65];
	// default encryption keys for backups without a password
	char cfg_defkeys[8][65];
	// key derivation function for encrypted configuration files.
	// return false if key derivation failed.
	bool (*cfg_keyfun)(const char *password, unsigned char *key, size_t size);
	// address spaces that can be dumped
	struct bcm2_typed_val options[8];
	struct bcm2_addrspace spaces[8];
	struct bcm2_version versions[8];
};

extern struct bcm2_profile bcm2_profiles[];

struct bcm2_profile *bcm2_profile_find(const char *name);
struct bcm2_addrspace *bcm2_profile_find_addrspace(
		struct bcm2_profile *profile, const char *name);
struct bcm2_partition *bcm2_addrspace_find_partition(
		struct bcm2_addrspace *addrspace, const char *name);

#ifdef __cplusplus
}


namespace bcm2dump {

class func
{
	public:
	func(uint32_t addr = 0, uint32_t args = 0, uint32_t intf = 0, uint32_t retv = 0)
	: m_addr(addr), m_args(args), m_intf(intf), m_retv(retv) {}

	uint32_t addr() const
	{ return m_addr; }

	uint32_t args() const
	{ return m_args; }

	uint32_t intf() const
	{ return m_intf; }

	uint32_t retv() const
	{ return m_retv; }

	const std::vector<const bcm2_patch*>& patches() const
	{ return m_patches; }

	std::vector<const bcm2_patch*>& patches()
	{ return m_patches; }

	private:
	uint32_t m_addr;
	uint32_t m_args;
	uint32_t m_intf;
	uint32_t m_retv;
	std::vector<const bcm2_patch*> m_patches;
};

class profile;

class version
{
	public:
	typedef std::map<std::string, uint32_t> u32map;
	typedef std::map<std::string, func> funcmap;

	version()
	: m_p(nullptr), m_prof(nullptr), m_def(nullptr)
	{}

	version(const bcm2_version* v, const profile* p, const bcm2_version* def)
	: m_p(v), m_prof(p), m_def(def ? def : v)
	{
		parse_codecfg();
		parse_functions();
	}

	std::string name() const
	{ return m_p ? (m_p->version[0] ? m_p->version : "any") : ""; }

	int intf() const
	{ return m_p->intf; }

	const bcm2_magic* magic() const
	{ return &m_p->magic; }

	u32map codecfg() const
	{ return m_codecfg; }

	uint32_t codecfg(const std::string& str) const
	{
		auto iter = m_codecfg.find(str);
		return iter != m_codecfg.end() ? iter->second : 0;
	}

	funcmap functions(const std::string& space) const
	{
		auto iter = m_functions.find(space);
		return iter != m_functions.end() ? iter->second : funcmap();
	}

	bool has_opt(const std::string& name) const
	{ return get_opt(name, BCM2_TYPE_NIL); }

	uint32_t get_opt_num(const std::string& name) const
	{ return get_opt(name, BCM2_TYPE_U32)->val.n; }

	uint32_t get_opt_num(const std::string& name, uint32_t def) const
	{ return has_opt(name) ? get_opt_num(name) : def; }

	std::string get_opt_str(const std::string& name) const
	{ return get_opt(name, BCM2_TYPE_STR)->val.s; }

	std::string get_opt_str(const std::string& name, const std::string& def) const
	{ return has_opt(name) ? get_opt_str(name) : def; }

	const bcm2_version* raw() const
	{ return m_p; }

	private:
	void parse_codecfg();
	void parse_functions();
	const bcm2_typed_val* get_opt(const std::string& name, bcm2_type type) const;

	const bcm2_version* m_p;
	const profile* m_prof;
	const bcm2_version* m_def;
	u32map m_codecfg;
	std::map<std::string, funcmap> m_functions;
};

class addrspace
{
	public:
	class part
	{
		public:
		friend class addrspace;

		part() : m_p(nullptr) {}

		std::string name() const
		{ return m_p ? m_p->name : ""; }

		uint32_t offset() const
		{ return m_p->offset; }

		uint32_t size() const
		{ return m_p->size; }

		uint32_t end() const
		{ return offset() + size(); }

		std::string altname() const
		{ return m_p->altname[0] ? m_p->altname : name(); }

		private:
		part(const bcm2_partition* p) : m_p(p) {}

		const bcm2_partition* m_p;
	};

	addrspace(const bcm2_addrspace* a, const profile& p);
	addrspace() {}

	std::string name() const
	{ return m_p->name; }

	bool is_mem() const
	{ return m_p->mem || is_ram(); }

	bool is_ram() const
	{ return name() == "ram"; }

	bool is_writable() const
	{ return is_ram() || m_p->mem == BCM2_MEM_RW; }

	int interfaces() const
	{ return m_p->intf; }

	uint32_t min() const
	{ return m_p->min; }

	uint32_t end() const
	{ return m_p->min + m_size; }

	uint32_t size() const
	{ return m_size; }

	unsigned alignment() const
	{ return !m_p->alignment ? (is_mem() ? 4 : 1) : m_p->alignment; }

	const std::vector<part>& partitions() const
	{ return m_partitions; }

	const part& partition(const std::string& name) const;
	const part& partition(uint32_t offset) const;

	func get_read_func(bcm2_interface intf) const;
	func get_write_func(bcm2_interface intf) const;
	func get_erase_func(bcm2_interface intf) const;

	bool check_offset(uint32_t offset, bool exception = true) const
	{ return check_range(offset, 0, "", exception); }

	uint32_t check_offset(uint32_t offset, const std::string& name) const
	{
		check_range(offset, 0, name, true);
		return offset;
	}

	uint32_t check_offset(uint32_t offset, const char* name) const
	{
		return check_offset(offset, std::string(name));
	}

	bool check_range(uint32_t offset, uint32_t length, bool exception = true) const
	{ return check_range(offset, length, "", exception); }

	void check_range(uint32_t offset, uint32_t length, const char* name) const
	{ check_range(offset, length, name, true); }

	void check_range(uint32_t offset, uint32_t length, const std::string& name) const
	{ check_range(offset, length, name, true); }

	private:
	bool check_range(uint32_t offset, uint32_t length, const std::string& name, bool exception) const;

	const bcm2_addrspace* m_p = nullptr;
	uint32_t m_size = 0;
	uint32_t m_kseg1 = 0;
	std::string m_profile_name;
	std::vector<part> m_partitions;
	std::vector<func> m_read_funcs;
	std::vector<func> m_write_funcs;
	std::vector<func> m_erase_funcs;
};

class profile
{
	public:
	typedef std::shared_ptr<profile> sp;

	virtual ~profile() {}

	virtual std::string name() const = 0;
	virtual std::string pretty() const = 0;
	virtual bool mipsel() const = 0;
	virtual unsigned baudrate() const = 0;
	virtual uint16_t pssig() const = 0;
	virtual uint16_t blsig() const = 0;
	virtual uint32_t kseg1() const = 0;
	virtual std::vector<const bcm2_magic*> magics() const = 0;
	virtual std::vector<version> versions() const = 0;
	virtual const version& default_version(int intf) const = 0;
	virtual std::vector<addrspace> spaces() const = 0;
	virtual const addrspace& space(const std::string& name, bcm2_interface intf) const = 0;
	virtual const addrspace& ram() const = 0;
	virtual bcm2_arch arch() const = 0;

	virtual std::string md5_key() const = 0;

	virtual uint32_t cfg_encryption() const = 0;
	virtual uint32_t cfg_padding() const = 0;
	virtual uint32_t cfg_flags() const = 0;

	//virtual std::string encrypt(const std::string& buf, const std::string& key) = 0;
	//virtual std::string decrypt(const std::string& buf, const std::string& key) = 0;

	virtual std::vector<std::string> default_keys() const = 0;
	virtual std::string derive_key(const std::string& pw) const = 0;

	void print_to_stdout(bool verbose = false) const;

	static const sp& get(const std::string& name);
	static const std::vector<profile::sp>& list();

	static void parse_opt_override(const std::string& str);

	friend class version;

	private:
	static std::vector<profile::sp> s_profiles;
	static std::map<std::string, bcm2_typed_val> s_overrides;
};

uint32_t magic_size(const bcm2_magic* magic);
std::string magic_data(const bcm2_magic* magic);

std::string get_profile_names(unsigned width, unsigned indent);
}

#endif
#endif
