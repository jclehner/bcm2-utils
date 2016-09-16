File format specification
=========================

This document describes the format used to store configuration data
in permnv/dynnv and the firmware settings dump (aka `GatewaySettings.bin`).

All numbers are stored in network byte order (big endian).

Header
------

### Permnv/dynnv

| Offset | Type        | Name       | Comment            |
|-------:|-------------|------------|--------------------|
|    `0` | `byte[202]` | `magic`    | all `\xff`         |
|  `202` | `u32`       | `size`     |                    |
|  `206` | `u32`       | `checksum` |                    |
|  `210` |             | `data`     |                    |

The value of `size` describes the number of bytes in the `data` section. The checksum is
calculated using a CRC-32 on `data`.


### GatewaySettings.bin

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







