File format specification
=========================

This document describes the format used to store configuration data
in permnv/dynnv and the firmware settings dump (aka `GatewaySettings.bin`).

All numbers are stored in network byte order (big endian).

Header
------

### Permnv/dynnv

| Offset | Type        | Name       | Comment            |
|--------|-------------|---------------------------------|
|    `0` | `byte[202]` | `magic`    | all `\xff`         |
|  `202` | `u32`       | `size`     ||
|  `206` | `u32`       | `checksum` ||
|  `210` |             | `data`     ||

`size` contains the number of bytes in the `data` section.
`checksum` is a CRC-32 checksum of `data`.


### GatewaySettings.bin

| Offset | Type        | Name       | Comment              |
|--------|-------------|-----------------------------------|
|    `0` | `byte[16]`  | `checksum` ||
|   `16` | `string[74]`| `magic`    | `6u9E9eWF0bt9Y8Rw690Le4669JYe4d-056T9p4ijm4EA6u9ee659jn9E-54e4j6rPj069K-670` |
|   `90` | `version`   | `version`  ||
|   `92` | `u32`       | `size`     ||
|   `96` |             | `data`     ||

