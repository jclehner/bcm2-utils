Firmware
========

Contrary to popular belief, the cable modem firmware is NOT based on Linux, but eCOS. This means that the
firmware is essentially just one big application, running in RAM, without a file system.

Some devices contain two processors with shared RAM, where Linux runs on the other processor, providing
NAS or media server features. An interface exists for passing commands between the two systems.

While previous Broadcom chipsets were MIPS only, the BCM3390 SoC uses an ARM Cortex-A15 quad-core
CPU for running Linux (used for all routing aspects), while the CM firmware itself still runs on a
MIPS core (handling all DOCSIS related stuff).

## File format

Magic files for [binwalk](https://github.com/OSPG/binwalk) are available
[in the repository](https://github.com/jclehner/bcm2-utils/tree/master/misc).

Firmware files are usually encapsulated in Broadcom's [ProgramStore](https://github.com/Broadcom/aeolus/tree/master/ProgramStore) format,
which uses a 92-byte header. Since
the header signature is device dependent, these headers are not that easy
to spot. Things to look out for are the `addr` (which often
is `0x80004000` for CM firmware images), or `name` fields (recognizable
by the NUL padding). Some images use `ecram_sto.bin` for the CM firmware
images. The header format is detailed below (numbers are big endian):

| Offset | Type     | Name | Comment                                     |
|-------:|----------|------|---------------------------------------------|
| 0      | `u16`    | sig  | Unique signature, (device dependent)        |
| 2      | `u16`    | ctrl | Control flags (compression, split image, etc.)|
| 4      | `u16`    | maj  | Major version |
| 6      | `u16`    | min  | Minor version |
| 8      | `u32`    | time | Build timestamp |
| 12     | `u32`    | len  | Image size (including this header) |
| 16     | `u32`    | addr | Image load address |
| 20     |`byte[48]`| name | Image name |
| 68     |`byte[8] `|      | Reserved |
| 76     | `u32`    | len1 | Image 1 size (for split images) |
| 80     | `u32`    | len2 | Image 2 size (for split images) |
| 84     | `u16`    | hcs  | Header checksum (CRC 16 CCITT)  |
| 86     | `u16`    |      | Reserved |
| 88     | `u32`    | chk  | Image checksum (CRC32 of data following this header) |

See the corresponding [source file](https://github.com/Broadcom/aeolus/blob/master/ProgramStore/ProgramStore.h).

Note that some non-split images have the `len1` field set to the same value as `len`. Others have it set to `0`.

The low byte of the `ctrl` field encodes the compression algorithm:

| n      | Compression
|-------:|------------|
| 0      | (none)     |
| 1      | LZ         |
| 2      | LZO        |
| 3      | (reserved) |
| 4      | [NRV2B](http://www.oberhumer.com/opensource/ucl/) |
| 5      | LZMA       |

The high byte of `ctrl` encodes the image type: `0x00` is a regular image, `0x01` is a dual (split) image.


###### Signed firmware files

Firmware files are may be preceeded by a code-signing certificate. In these files,
the actual data is preceeded by [ASN.1](https://en.wikipedia.org/wiki/Abstract_Syntax_Notation_One) encoded data,
and usually starts with `0x30 0x82` (`SEQUENCE`, 2 byte length field follows). For example, if a file starts
with `0x30 0x82 0x06 0x0d`, skip to offset `4 + 0x60d`, where you should find the end-of-data marker `0x00 0x00`.

###### Monolithic image

Some firmware files are so-called monolithic images, which contain multiple image files. In these file, an additional
16-byte header is found before the first ProgramStore header:

| Offset | Type       | Name  | Comment                                     |
|-------:|------------|-------|---------------------------------------------|
| 0      | `u32`      | magic | `0x4d4f4e4f` (`MONO`)                       |
| 4      | `u16`      | sig   | Device-dependent signature (similar to ProgramStore signature) |
| 6      | `byte`     | vmaj  | Major version                               |
| 7      | `byte`     | vmin  | Minor version
| 8      | `u32`      | len   | Image size (including this header)          |
| 12     | `u32`      | images| Bitmask of image numbers contained in this file. |

Individual images are padded to a 64k (or 32k?) block size.

The exact meaning of the image numbers specified im `images` varies, depending
on the platform, although though `0` is usually the bootloader. Below are
examples for the BCM338x and BCM3390 platforms:

| n      | BCM338{3,4}   | BCM3390   |
|-------:|---------------|-----------|
| 0      | `bootloader`  | `BOOTL`   |
| 1      | `image1`      | `DOCSIS`  |
| 2      | `image2`      | `CM`      |
| 3      | `linux`       | `RG`      |
| 4      | `linuxapps`   | `STB`     |
| 5      | `permv`       | `APPS`    |
| 6      | `dhtml`       | `BOLT`    |
| 7      | `dynnv`       | `DEVTREE` |
| 8      | `linuxkfs`    | `HYP`     |
| 9      | N/A           | `KERNEL`  |
| 10     | N/A           | `PCI`     |

###### ProgramStore image

ProgramStore images may contain data for various purposes. So far,
the following contents have been observed:

* CM firmware images. In these cases, the `addr` field is non-zero. This is
  raw machine code, not a specific executable file format!
* `tar.gz` files, containing Linux device tree information
* Linux `zImage`
* UBI - UBIFS image (Linux filesystem)
* UBI - SQUASHFS image (Linux filesystem)
* UBI - SQUASHFS image (CM firmware images)

Extracting UBI stuff is tricky. Some options are:

* [nandsim](http://www.linux-mtd.infradead.org/faq/nand.html) (most reliable)
* [ubi_reader](https://github.com/jrspruitt/ubi_reader/blob/master/README.md)

(detailed instructions coming soon).
