File format specification
=========================

This document describes the format used to store configuration data
in permnv/dynnv and the firmware settings dump (aka `GatewaySettings.bin`).

All numbers are stored in network byte order (big endian).

Header
------

### Permnv/dynnv

There are two variants. The "old style" is used on pre-BCM3390 chipsets,
and the "new style" used on BCM3390 (and probably future chipsets).

Both styles use the same header format:

| Offset | Type         | Name       |
|-------:|--------------|------------|
|  `0`   | `u32`        | `size`     |
|  `4`   | `u32`        | `checksum` |
|  `8`   |`byte[size-8]`| `data`     |

To calculate the checksum, `checksum` is first set to zero,
then, starting at `size`, the following algorithm is employed:
(Broadcom calls this `HCS-32`, apparently).

```c
uint32_t hcs32(const char* buf, size_t len)
{
	uint32_t checksum = 0;

	while (len >= 4) {
		checksum += ntohl(*(uint32_t*)buf);
		buf += 4;
		len -= 4;
	}

	uint16_t half;

	if (len >= 2) {
		half = ntohs(*(uint16_t*)buf);
		buf += 2;
		len -= 2;
	} else {
		half = 0;
	}

	uint8_t byte = len ? *(uint8_t*)buf : 0;

	checksum += (byte | (half << 8)) << 8;
	return ~checksum;
}
```

For a buffer containing the data `\xaa\xaa\xaa\xaa\xbb\xbb\xbb\xbb\xcc\xcc\xdd`, the
checksum is thus `~(0xaaaaaaaa + 0xbbbbbbbb + 0xccccdd00)`, for `\xaa\xaa\xaa\xaa\xbb`,
it would be `~(0xaaaaaaaa + 0x0000bb00)` (assuming `uint32_t` rollover on overflow).

To simply check for a valid checksum, the `checksum` field is left as is. In this case,
the result would be `0`, if the checksum is valid.

##### Old style (pre BCM3390)

Both `permnv` and `dynnv` reside in raw flash partitions. The actual header is always
prefixed by 202 `\xff` bytes.

As part of a backup and wear-leveling mechanism, each partition actually contains
multiple copies of the data. The partition's 8 last bytes form a footer, that is used
by the firmware to determine which copy is the active one.

| Offset | Type         | Name                               |
|-------:|--------------|------------------------------------|
|  `-8`  | `u32`        | `segment_size`                     |
|  `-4`  | `byte[2]`    | (used for alignment)               |
|  `-2`  | `i16`        | `segment_bitmask`                  |

`segment_size` is the total size of the active segment, including the 202-byte
prefix (see above), plus padding.

The name `segment_bitmask` is a bit of a misnomer, but it is referred to as such
by the firmware. For a partition that's never been written, the value is `0xfff8`
(i.e. `-8`). Each time new data is written to the device, this value is multiplied
by `2`, so after the first write, it's `0xfff0` (`-16`), then `0xffe0` (`-32`), and
so on.

The offset of the active settings data is also determined by this
"bitmask", using the following formula:

```
segment_offset = segment_size * (log2(-segment_bitmask) - 1)
```

##### New style (BCM3390)

On these devices, the CM firmware "nonvol" files are stored on a JFFS2 partition. The files are
named `cm_perm.bin` and `cm_dyn.bin`, for `permnv` and `dynnv`, respectively.

All data after the checksum is encrypted using AES-256-ECB, with a key that is unique to each
device. It can be read from offset memory offset `0xd384bfe0`, from both the MIPS (=CM firmware)
and ARM (=BOLT + Linux) side.

### GatewaySettings.bin (standard)

| Offset  | Type        | Name       | Comment                  |
|--------:|-------------|------------|--------------------------|
|    `0`  | `byte[16]`  | `checksum` |                          |
|   `16`  | `string[74]`| `magic`    | See below                |
|   `90`  | `byte[2]`   | ?          | Version? Usually `0.0`   |
|   `92`  | `u32`       | `size`     ||
|   `96`  | `byte[size]`| `data`     ||
|`96+size`| `byte[]`    | `padding`  | For encrypted files      |

The above offsets assume a "normal" 74-character magic value. 

#### Magic

At 74 characters, the magic string is unusually long. It's composed of
alphanumeric characters, separated by dashes, and is usually the same
for one manufacturer. Currently known magic values:

| Vendor                    | Magic                                                                        |
|---------------------------|-----------------------------------------------------------------------------:|
| Technicolor/Thomson       | `6u9E9eWF0bt9Y8Rw690Le4669JYe4d-056T9p4ijm4EA6u9ee659jn9E-54e4j6rPj069K-670` |
| Netgear, Motorola, Ubee   | `6u9e9ewf0jt9y85w690je4669jye4d-056t9p48jp4ee6u9ee659jy9e-54e4j6r0j069k-056` |
| Sagem (3284?)             | `6u9e9ewf0jt9y85w690je4669jye4d-056t9p48jp4ee6u9ee659jy9e-54e4j6r0j069k-057` |
| Sagem 3686                | `FAST3686<isp>056t9p48jp4ee6u9ee659jy9e-54e4j6r0j069k-056`                   |
| Sagem 3286                | `FAST3286<isp>056t9p48jp4ee6u9ee659jy9e-54e4j6r0j069k-056`                   |

Some Sagem modems use device- *and* ISP-dependent magic values, denoted as
`<isp>` in the list above. Currently known ISP values are:

| ISP            | Magic         |
|----------------|---------------|
| DNA Oyj (FI)   | `DNA`         |
| Claro (BR)     | `CLARO`       |
| SFR (FR)       | `SFR-PC20`    |

#### Checksum

The checksum is an MD5 hash, calculated from a device-specific salt, plus the file contents immediately
following the checksum (i.e. everything but the first 16 bytes).

The salt is a 16-byte string that is the same across most vendors and devices. Thomson/Technicolor however
use device-specific salts, that can be easily guessed however:

| Vendor      | Device       | Salt                                                               |
|-------------|--------------|--------------------------------------------------------------------|
| Generic     | Generic      | `\x32\x50\x73\x6c\x63\x3b\x75\x28\x65\x67\x6d\x64\x30\x2d\x27\x78` |
| Thomson     | TWG850-4     | `TMM_TWG850-4\x00\x00\x00\x00` |
|             | TWG870       | `TMM_TWG870\x00\x00\x00\x00\x00\x00` |
|             | TCW770       | `TMM_TCW770\x00\x00\x00\x00\x00\x00` |
| Technicolor | TC7200       | `TMM_TC7200\x00\x00\x00\x00\x00\x00` |

#### Encryption

Some device firmwares encrypt their GatewaySettings.bin files. However, as this doesn't seem to be a feature
of the Broadcom firmware itself, implementations vary greatly among different vendors, and sometimes even
different devices. All currently known encryption schemes are listed below:


| Vendor              | Model       | Cipher      | Padding    | Extent           | Features    |
|---------------------|-------------|-------------|------------|------------------|-------------|
| Thomson/Technicolor |             | AES-256-ECB | [custom](#motorolaarris-custom-encryption)     | header, settings | `password`  |
| Ubee                |             | AES-128-CBC | ANSI-X9.23 | full             | `lenprefix` |
| Netgear, Asus       |             | 3DES-ECB    | PKCS#7     | full             |             |
| Motorola/Arris      |             | [custom](#thomsontechnicolor-custom-padding)      | N/A        | full             |             |     
| NetMASTER           | CBW-383ZN   | DES-ECB     | ?          | full             |             |
| Sagem               | F@st3686 AC | XOR 0x80    | N/A        | full             | `circumfix` |
|                     | F@st3284    | ?           | ?          | settings         | ?           | 


The "extent" column indicates which portions of the file are encrypted. If we think of a file
as being composed of prefix, checksum, header, settings, and suffix (in this order). "Full" in this
context means the file data without pre- and suffixes.


The following special features are currently known:

* `password`: aside from a default key, the firmware allows specifying a backup password
* `lenprefix`: encrypted data is preceeded by a 32-bit length prefix
* `circfix`: encrypted data is preceeded and followed by a static 12-byte sequence. the actual
             meaning is unknown, but remains the same on one device.

Custom ciphers and padding schemes are explained in the following paragraphs.

###### Motorola/Arris custom encryption
A XOR pad. The pad is generated using the result of a custom `rand()` implementation and floating point math.
The seed used is the current time `& 0xff`. As a result, there are 256 possible encrypted files for the same data,
depending on the time at the point of creation. The actual seed used is appended to the encrypted data. An
implementation is shown below:

```c
uint32_t my_srand;

int32_t my_rand()
{
	uint32_t result, next = srand;

	next *= 0x41c64e6d;
	next += 0x3039;
	result = next & 0xffe00000;

	next *= 0x41c64e6d;
	next += 0x3039;
	result += (next & 0xfffc0000) >> 11;

	next *= 0x41c64e6d;
	next += 0x3039;
	result = (result + (next >> 25)) & 0x7fffffff;

	srand = next;
	return result;
}

void encrypt(char* buf, size_t size, uint8_t seed)
{
	srand = seed;

	for (size_t i = 0; i < size; ++i) {
		buf[i] ^= (((double)rand() / 0x7fffffff) * 255) + 1;
	}
}
```

###### Thomson/Technicolor custom padding

If the final block is less than 16 bytes, the data is copied verbatim, thus leaking some information:

```
...
000041a0  80 d3 e4 8a 71 51 f2 64  81 e4 31 4a 64 a9 5d 74  |....qQ.d..1Jd.]t|
000041b0  69 6e 00 05 61 64 6d 69  6e                       |in..admin|
```

Some firmware versions append a 16-byte block of all `\x00` before encrypting, so as to "leak" only
zeroes:

```
...
000041a0  80 d3 e4 8a 71 51 f2 64  81 e4 31 4a 64 a9 5d 74  |....qQ.d..1Jd.]t|
000041b0  b3 65 87 cd ad 42 6c d1  af 3c 63 a9 20 b1 b9 6c  |.e...Bl..<c. ..l|
000041c0  00 00 00 00 00 00 00 00  00                       |.........|
```

### GatewaySettings.bin (dynnv)

| Offset | Type         | Name       |
|-------:|--------------|------------|
|  `0`   | `u32`        | `size`     |
|  `4`   | `u32`        | `checksum` |
|  `8`   |`byte[size-8]`| `data`     |

The header for this file format is the same as for `permnv` and `dynnv`, minus
the 202-byte all `\xff` header.

###### Encryption

A primitive (and obvious) subtraction cipher with 16 keys is sometimes used. The keys are:

```
00 00 02 00 04 00 06 00 08 00 0a 00 0c 00 0e 00
10 00 12 00 14 00 16 00 18 00 1a 00 1c 00 1e 00
20 00 22 00 24 00 26 00 28 00 2a 00 2c 00 2e 00
...
f0 00 f2 00 f4 00 f6 00 f8 00 fa 00 fc 00 fe 00
```
For the first 16-byte block, the first key is used, the second key for the second block,
and so forth. For the 17th block, the first key is used again. If the last block is less
than 16 bytes, it is copied verbatim. A 36 byte all-zero file would thus be encrypted as

```
00 00 02 00 04 00 06 00 08 00 0a 00 0c 00 0e 00
10 00 12 00 14 00 16 00 18 00 1a 00 1c 00 1e 00
00 00 00 00
```

After applying the subtraction cipher, bytes `n` and `n+1` are swapped.

Configuration data
------------------

Aside from the header, `GatewaySettings.bin` and permnv/dynnv use the same format. The
configuration data consists of a series of settings groups, each preceeded by a group
header.

### Group header

| Offset | Type          | Name       |
|-------:|---------------|------------|
|    `0` | `u16`         | `size`     |
|    `4` | `byte[4]`     | `magic`    |
|    `6` | `byte[2]`     | `version`  |
|    `8` | `byte[size-8]`| `data`     |

Value of `size` is the number of bytes in this group, including the full header. An empty
settings group's size is thus `8` bytes. The `magic` is often either a human-readable string
(`802T`: Thomson Wi-Fi settings, `CMEV`: CM event log) or a hexspeak `u32` (`0xd0c20130`: DOCSIS 3.0 settings,
`0xf2a1f61f`: HAL interface settings).

### Group data

Since the groups contain variable-length values, to interpret a specific variable, the type of all preceeding
variables must be known. Within the same device, newer group versions will place new variables at the end, but
this may not be true for group data on different devices.

##### Data types

###### Numbers

Always stored in network byte order; `uN` for unsigned N-bit integers, `iN` for signed N-bit integers. Enum and
bitmask types based on `uN` types are also available.

###### Strings

Various methods are used to store strings, with some groups often showing a preference for one kind of encoding.
The following table shows various string samples (`\x??` means *any* byte, `(N)` means width `N`):

| Type       | Description                                 | `""`               | `"foo"`                        |
| -----------|---------------------------------------------|--------------------|--------------------------------|
|`fstring`   | Fixed-width string, with optional NUL byte  | `\x00\x00`(2)   |`foo`(3), `foo\x00\x??\x??`(6)|
|`fzstring`  | Fixed-width string, with mandatory NUL byte | `\x00\x00`(2)   | `foo\x00`(4), `foo\x00:??`(5)|
|`zstring`   | NUL-terminated string                       | `\x00`             | `foo\x00`                      |
|`p8string`  | `u8`-prefixed string, with optional NUL byte| `\x00`             |`\x03foo`, `\x04foo\x00`        |
|`p8zstring` | `u8`-prefixed string with mandatory NUL byte| `\x00`             | `\x04foo\x00`                  |
|`p8istring` | `u8`-prefixed string with optional NUL byte, size includes prefix | `\x00` | `\x04foo`, `\x05foo\x00`|
|`p16string` | `u16`-prefixed string, with optional NUL byte | `\x00\x00`       |`\x00\x03foo`, `\x00\x04foo\x00`|
|`p16zstring`| `u16`-prefixed string with mandatory NUL byte | `\x00\x00`       |`\x00\x04foo\x00`               |
|`p16istring`| `u16`-prefixed string with optional NUL byte, size includes prefix |`\x00\x00`| `\x00\x05foo`,`\x00\x06foo\x00`|

###### Lists

| Type       | Description                                                                    |
| -----------|--------------------------------------------------------------------------------|
| `array`    | Fixed-size array (element number is fixed, but not actual size in bytes)       |
| `pNlist`   | `u8`- or `u16`-prefixed list; prefix contains number of elements in list       |

Even though an `array` always has a fixed length defined in code, some elements may be considered
undefined, i.e. the *apparent* length of the array may be less. In some settings groups, "dummy"
entries are used to mark undefined elements (e.g. MAC adddress `00:00:00:00:00:00`, IPv4 `0.0.0.0`,
string `""`, etc.) while in others, the apparent length is stored in another variable within the
same group (but not as a prefix).

Sample encodings for string arrays/lists:

| Type                   | `{ "foo", "ba", "r" }`            | `{ "", "" }`                       |
|------------------------|-----------------------------------|------------------------------------|
| `array<fzstring<4>>`   | `foo\x00ba\x00\x00r\x00\x00\x00`  | `\x00\x00\x00\x00\x00\x00\x00\x00` |
| `array<zstring>`       | `foo\x00ba\x00r\x00`              | `\x00\x00`                         |
| `array<p8string>`      | `\x03foo\x02ba\x01r`              | `\x00\x00`                         |
| `p8list<zstring>`      | `\x03foo\x00ba\x00r\x00`          | `\x02\x00\x00`                     |
| `p8list<p8string>`     | `\x03\x03foo\x02ba\x01r`          | `\x02\x00\x00`                     |
| `p16list<fstring<3>>`  | `\x00\x03fooba\x00r\x00\x00`      | `\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00` |

Sample encodings for integer arrays/lists:

| Type                   | `{ 0xaaaa, 0, 0xbbbb  }`                   | `{ 0xaa }`, width 2, dummy `0` | `{}`      |
|------------------------|--------------------------------------------|--------------------------------|-----------|
| `array<u16>`           | `\xaa\xaa\x00\x00\xbb\xbb`                 |`\x00\xaa\x00\x00`              | N/A       |
| `array<u32>`           | `\x00\x00\xaa\xaa\x00\x00\x00\x00\xbb\xbb` |`\x00\x00\x00\xaa\x00\x00\x00\x00`| N/A     |
| `p8list<u16>`          | `\x03\xaa\xaa\x00\x00\xbb\xbb`             | N/A                            | `\x00`    |
| `p16list<u16>`         | `\x00\x03\xaa\xaa\x00\x00\xbb\xbb`         | N/A                            | `\x00\x00`|
