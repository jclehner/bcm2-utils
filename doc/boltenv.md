BOLT environment variable store
===============================

This format is used by Broadcom's BOLT bootloader on ARM platforms. The environment variables
are stored on a separate flash partition. Note that this partition may be encrypted using a
device-specific unique key.

Numbers are stored in little-endian, unless otherwise noted.

| Offset | Type     | Name        | Comment                                     |
|-------:|----------|-------------|---------------------------------------------|
| 0      | u8[4]    | tlv_cheat   | `[ 0x01, 0x1a, 0x00, 0x00 ]`                |
| 4      | u32      | magic       | `0xbabefeed`                                |
| 8      | u32      | ?           |                                             |
| 12     | u32      | ?           |                                             |
| 16     | u32      | write_count |                                             |
| 20     | u32      | size        |                                             |
| 24     | u32      | checksum    |                                             |
| 28     | u8[size] | variables   |                                             |

#### Variables

Environment variables are stored using the same TLV encoding as used by Broadcom's
[CFE bootloader](https://github.com/blackfuel/asuswrt-rt-ax88u/blob/master/release/src-rt-5.02axhnd/cfe/cfe/main/env_subr.c).

The `tlv_cheat` field makes the boltenv header appear as if it was just another data blob: type `0x01`, size `0x1a` (two padding
`0x00`s + actual header size).

A type of `0x00` means end of data. For the other types, the `value` field contains a `name=value` string, where everything up
to the first `=` sign is interpreted as the variable name. The code of Broadcom's `boltenv` tool doesn't make any effort to validate
the characters used as the variable name.

Variables that have the "temporary" flag set aren't saved when the variable store is committed to storage (so you won't usually encounter
them in a boltenv partition).

###### Type `0x01` (8 bit length prefix)

| Offset | Type       | Name        | Comment                                     |
|-------:|------------|-------------|---------------------------------------------|
| 0      | u8         | type        | `0x01`                                      |
| 1      | u8         | length      |                             |
| 2      | u8         | flags       | Bitmask: `0x01` = temporary, `0x02` = ro    |
| 3      | u8[size-1] | value       |                                             |

###### Type `0x02` (16 bit length prefix)

| Offset | Type       | Name        | Comment                                     |
|-------:|------------|-------------|---------------------------------------------|
| 0      | u8         | type        | `0x02`                                      |
| 1      | u16be      | length      | big endian!                                 |                               
| 3      | u8         | flags       | (same as above)                             |
| 4      | u8[size-1] | value       |                                             |

This type may be non-standard, as some `boltenv` utilities just skip over them.
