BOLT environment variable store
===============================

This format is used by Broadcom's BOLT bootloader on ARM platforms. The environment variables
are stored on a separate flash partition. Note that this partition may be encrypted using a
device-specific unique key.

Numbers are stored in little-endian, unless otherwise noted.

| Offset | Type     | Name        | Comment                                     |
|-------:|----------|-------------|---------------------------------------------|
| 0      | u32      | tlv_cheat   | Always `0x1a01`                             |
| 4      | u32      | magic       | Always `0xbabefeed`                         |
| 8      | u32      | ?           |                                             |
| 12     | u32      | ?           |                                             |
| 16     | u32      | write_count |                                             |
| 20     | u32      | size        |                                             |
| 24     | u32      | checksum    |                                             |
| 28     | u8[size] | variables   |                                             |

The data is made up of multiple blocks, each preceded by a one byte type specifier.
A type specifier of `0x00` means end of data. The following types are currently known:

###### Type `0x01` (short variable)

| Offset | Type       | Name        | Comment                                     |
|-------:|------------|-------------|---------------------------------------------|
| 0      | u8         | type        | Always `0x01`                               |
| 1      | u8         | size        |                             |
| 2      | u8         | flags       | Bitmask: `0x01` = temporary, `0x02` = ro    |
| 3      | u8[size-1] | data        |                                             |

The `data` field contains a `name=value` string, where everything up to the first `=` sign is
interpreted as the variable name. The code of Broadcom's `boltenv` tool doesn't make any effort
to validate the characters used as the variable name.

Variables marked temporary aren't saved when the variable store is committed to storage.

###### Type `0x02` (long variable?)

| Offset | Type       | Name        | Comment                                     |
|-------:|------------|-------------|---------------------------------------------|
| 0      | u8         | type        | Always `0x02`                               |
| 1      | u16be      | size        | (big endian!)                               |
| 3      | u8[size]   | data        |                                             |

This type may be non-standard, as some `boltenv` utilities just skip over them.
