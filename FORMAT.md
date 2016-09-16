File format specification
=========================

This document describes the format used to store configuration data
in permnv/dynnv and the firmware settings dump (aka `GatewaySettings.bin`).

All numbers are stored in network byte order (big endian).

Header
------

##### Permnv/dynnv

| Offset | Type        | Name       | Comment            |
|-------:|-------------|------------|--------------------|
|    `0` | `byte[202]` | `magic`    | all `\xff`         |
|  `202` | `u32`       | `size`     |                    |
|  `206` | `u32`       | `checksum` |                    |
|  `210` |             | `data`     |                    |

The value of `size` describes the number of bytes in the `data` section. The checksum is
calculated using a CRC-32 on `data`.


##### GatewaySettings.bin

| Offset | Type        | Name       | Comment              |
|-------:|-------------|------------|----------------------|
|    `0` | `byte[16]`  | `checksum` ||
|   `16` | `string[74]`| `magic`    | `6u9E9eWF0bt9Y8Rw690Le4669JYe4d-056T9p4ijm4EA6u9ee659jn9E-54e4j6rPj069K-670` |
|   `90` | `byte[2]`   | ?          | Assumed to be a version (always `0.0`) |
|   `92` | `u32`       | `size`     ||
|   `96` |             | `data`     ||

The checksum is an MD5 hash, calculated from the file contents immediately after the checksum (i.e.
everything but the first 16 bytes). To calculate the checksum, a 16-byte device-specific key is added
to the data; for some devices, this is easily guessed (Thomson TWG850-4: `TMM_TWG850-4\x00\x00\x00\x00`,
Techicolor TC7200: `TMM_TC7200\x00\x00\x00\x00\x00\x00`), for others, it must be extracted from a firmware dump
(e.g. Netgear CG3000: `\x32\x50\x73\x6c\x63\x3b\x75\x28\x65\x67\x6d\x64\x30\x2d\x27\x78`).


On some devices, all data *after* the checksum is encrypted using AES-256 in ECB mode. This means that the
checksum can be validated even if the encryption key is not known.

All currently known devices have a default encryption key, and some allow specifying a backup password. If the
final block is less than 16 bytes, the data is copied verbatim, thus leaking some information:

```
...
00004280  9f 60 b1 1b 54 28 35 8f  e4 39 57 76 da fd f2 24  |.`..T(5..9Wv...$|
00004290  00 05 61 64 6d 69 6e                              |..admin|
```

Some firmware versions append a 16-byte block of all `\x00` before encrypting, so as to "leak" only
zeroes:

```
...
00006690  a2 87 fc 07 86 b2 75 f1  4b 59 de 7b 74 c1 ac 90  |......u.KY.{t...|
000066a0  00 00 00 00 00 00 00 00                           |........|
```

Configuration data
------------------

Aside from the header, `GatewaySettings.bin` and permnv/dynnv use the same format. The
configuration data consists of a set of settings "groups".

##### Group header

| Offset | Type        | Name       |
|-------:|-------------|------------|
|    `0` | `u16`       | `size`     |
|    `4` | `byte[4]`   | `magic`    |
|    `6` | `byte[2]`   | `version`  |
|    `8` |             | `data`     |

Value of `size` is the number of bytes in this group, including the full header. An empty
settings group's size is thus `8` bytes. The `magic` is often either a human-readable string
(`802T`: Thomson Wi-Fi settings, `CMEV`: CM event log) or a hexspeak `u32` (`0xd0c20130`: DOCSIS 3.0 settings,
`0xf2a1f61f`: HAL interface settings).

##### Group data

The data contained in each group is a series of values, some of which are variable-length values. This means that
most variables will not have a fixed offset within a group, and a variable can only be successfully interpreted if
the meaning of all preceeding values is known. The following variable types are currently known:

###### Numbers

Always stored in network byte order; `uN` for unsigned N-bit integers, `iN` for signed N-bit integers.

###### Strings

Various methods are used to store strings, with some groups often showing a preference for one kind of encoding.
The following list shows different encodings for the string `"foo"`.

* `fstring`: Fixed-width string, with optional NUL byte (width 6: `\x66\x6f\x6f\x00\x??\x??`; width 3: `\x66\x6f\x6f`)
* `fzstring`: Fixed-width string, with mandatory NUL byte - maximum length is thus `width - 1` (width 4: `\x66\x6f\x6f\x00`)
* `zstring`: NUL-terminated string (`\x66\x6f\x6f\x00`)
* `p8string`: `u8`-prefixed string, with optional NUL byte (`\x03\x66\x6f\x6f` or `\x04\x66\x6f\x6f\x00`)
* `p8zstring`: `u8`-prefixed string with mandatory NUL byte (`\x04\x66\x6f\x6f\x00`)
* `p8istring`: `u8`-prefixed string with optional NUL byte, size includes prefix (`\x04\x66\x6f\x6f` or `\x05\x66\x6f\x6f\x00`)
* `p16string`: `u16`-prefixed string, with optional NUL byte (`\x00\x03\x66\x6f\x6f` or `\x00\x04\x66\x6f\x6f\x00`)
* `p16zstring`: `u16`-prefixed string with mandatory NUL byte (`\x00\x04\x66\x6f\x6f\x00`)
* `p16istring`: `u16`-prefixed string with optional NUL byte, size includes prefix (`\x00\x05\x66\x6f\x6f` or `\x00\x06\x66\x6f\x6f\x00`)












