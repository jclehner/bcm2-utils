Firmware file format
====================

Firmware files are usually encapsulated in Broadcom's [ProgramStore](https://github.com/Broadcom/aeolus/tree/master/ProgramStore) format,
which uses a 92-byte header. Since
the header signature is device dependent, these headers are not that easy
to spot. Things to look out for are the `addr` (which often
is `0x80004000` for CM firmware images), or `name` fields (recognizable
by the NUL padding). Some images use `ecram_sto.bin` for the CM firmware
images. The header format is detailed below (numbers are big endian):

| Offset | Type     | Name | Comment                                     |
|-------:|----------|------|---------------------------------------------|
| 0      | u16      | sig  | Unique signature, (device dependent)        |
| 2		 | u16	    | ctrl | Control flags (compression, image type, etc.|
| 4		 | u16      | maj  | Major version |
| 6		 | u16	    | min  | Minor version |
| 8		 | u32	    | time | Build timestamp |
| 12	 | u32	    | len  | Image size (including this header) |
| 16     | u32      | addr | Image load address |
| 20	 | byte[48] | name | Image name |
| 68     | byte[8]  |      | Reserved |
| 76     | u32      | len1 | Image 1 size (for split images) |
| 80     | u32		| len2 | Image 2 size (for split images) |
| 84     | u16      | hcs  | Header checksum (CRC 16 CCITT)  |
| 86     | u16      |      | Reserved |
| 88     | u32a     | chk  | Image checksum (CRC32 of data following this header) |

See the corresponding [source file](https://github.com/Broadcom/aeolus/blob/master/ProgramStore/ProgramStore.h).


###### Signed firmware files

Firmware files are may be preceeded by a code-signing certificate. In these files,
the actual data is preceeded by [ASN.1](https://en.wikipedia.org/wiki/Abstract_Syntax_Notation_One) encoded data,
and thus usually start with `0x30 0x82` (`SEQUENCE`, 2 byte length field follows). For example, if a file starts
with `0x30 0x82 0x06 0x0d`, skip to offset `4 + 0x60d`. Usually, there is a two-byte padding of `0x00 0x00` before
the actual image.

###### Monolithic image

Some firmware files are so-called monolithic images, which contain multiple image files. In these file, an additional
16-byte header is found before the first ProgramStore header:

| Offset | Type     | Name  | Comment                                     |
|-------:|----------|-------|---------------------------------------------|
| 0      | u32		| magic-| `0x4d4f4e4f` (`MONO`)                       |
| 4		 | u16		| sig   | Device-dependent signature (not neccessarily equal to ProgramStore signature) |
| 6      | byte[2]  | unk1  | Unknown                                     |
| 8      | u32      | len   | Image size (including this header)          |
| 12	 | byte[4]  | unk2  | Unknown                                     |

Individual images are padded with `0` bytes, to a 64k block size. It's possible that the amount of
padding is specified within the header. `unk1[1]` is `16` in all images I've seen so far, which
*could* mean 16 `4k` blocks (but this is purely speculative).

###### ProgramStore image

ProgramStore images may contain data for various purposes. So far,
the following contents have been observed:

* CM firmware images. In these cases, the `addr` field is non-zero. This is
  raw machine code, not a specific executable file format!
* `tar.gz` files, containing Linux device tree information
* Linux `zImage`
* UBI - UBIFS images (Linux filesystem)
* UBI - SQUASHFS images (Linux filesystem)
* UBI - SQUASHFS images (CM firmware images)

Extracting UBI stuff is tricky. Some options are:

* [nandsim](http://www.linux-mtd.infradead.org/faq/nand.html) (most reliable)
* [ubi_reader](ttps://github.com/jrspruitt/ubi_reader/blob/master/README.md)

(detailed instructions coming soon).
