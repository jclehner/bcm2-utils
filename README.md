**WARNING** Parts of this readme still refer to the old [legacy](https://github.com/jclehner/bcm2-utils/tree/legacy) branch.


# bcm2-utils

**Util**ities for **B**road**c**o**m**-based **c**able **m**odems.

* [bcm2dump](#bcm2dump): A utility to dump ram/flash, primarily intended as a firmware dump tool for cable modems based on a Broadcom SoC. Works over serial connection (bootloader, firmware) and telnet (firmware).
* [bcm2cfg](#bcm2cfg): A utility to modify/encrypt/decrypt the configuration
   file (aka `GatewaySettings.bin`), but also NVRAM images.

Fully supported devices:

* Technicolor TC7200 (bootloader, shell)
* Thomson TWG850-4 (shell)
* Thomson TWG870 (shell)

Partially supported:

* Thomson TCW770 (`bcm2cfg` only)

Binaries for Linux, OS X and Windows coming soon. `bcm2dump` is not yet available for Windows.

It should be easy to add support for other devices. Some pointers can
be found [below](#writing-a-device-profile).

These utilities are not yet stable - command line options are likely
to change. Bug reports are always welcome.

## bcm2dump
```
Usage: bcm2dump [<options>] <command> [<arguments> ...]

Options:
  -s               Always use safe (and slow) methods
  -R               Resume dump
  -F               Force operation
  -P <profile>     Force profile
  -q               Decrease verbosity
  -v               Increase verbosity

Commands: 
  dump  <interface> <addrspace> {<partition>[+<offset>],<offset>}[,<size>] <outfile>
  write <interface> <addrspace> {<partition>[+<offset>],<offset>}[,<size>] <infile>
  exec  <interface> {<partition>,<offset>}[,<entry>] <infile>
  info  <interface>
  help

Interfaces: 
  /dev/ttyUSB0             Serial console with default baud rate
  /dev/ttyUSB0,115200      Serial console, 115200 baud
  192.168.0.1,2323         Raw TCP connection to 192.168.0.1, port 2323
  192.168.0.1,foo,bar      Telnet, server 192.168.0.1, user 'foo', password 'bar'
  192.168.0.1,foo,bar,233  Same as above, port 233

bcm2dump e741871 Copyright (C) 2016 Joseph C. Lehner
Licensed under the GNU GPLv3; source code is available at
https://github.com/jclehner/bcm2utils
```

This utility can be used to dump firmware or other flash contents.
`bcm2dump` requires either an unlocked bootloader (serial connection),
or a working firmware shell (`CM>` prompt; serial and telnet supported).

Read/write speed varies, depending on the interface and source. The following
tables give a broad overview. "Fast" methods write machine code to the device,
which is then executed. Serial speeds are based on a baud-rate of `115200`.

###### Read speeds

|                        | ram        | ram (fast) | flash        | flash (fast) |
|-----------------------:|-----------:|-----------:|-------------:|-------------:|
| **bootloader (serial)**|   500  B/s |  4.4 KB/s  |     N/A      |    4.4 KB/s  |
| **firmware (serial)**  |   2.5 KB/s |    N/A     |    2.8 KB/s  |      N/A     |
| **firmware (telnet)**  | 20-50 KB/s |    N/A     |  20-50 KB/s  |      N/A     |

###### Write speeds

|                        | ram        | ram (fast) | flash        | flash (fast) |
|-----------------------:|-----------:|-----------:|-------------:|-------------:|
| **bootloader (serial)**|     12 B/s |    N/A     |     N/A      |      N/A     |
| **firmware (serial)**  |     18 B/s |    N/A     |       18 B/s |      N/A     |
| **firmware (telnet)**  |     18 B/s |    N/A     |       18 B/s |      N/A     |


Firmware images are usually in Broadcom's ProgramStore format. Utilities for
extraction and compression are available from Broadcom (and GPLv3'd!):

https://github.com/Broadcom/aeolus/tree/master/ProgramStore

##### Usage

Show device profile (and list partitions):
```
$ ./bcm2dump -P tc7200 info
tc7200: Technicolor TC7200
==========================
pssig         0xa825
blsig         0x3386

ram           0x80000000 - 0x87ffffff  (   128 MB)  RW
------------------------------------------------------
bootloader    0x83f80000 - 0x83f9ffff  (   128 KB)
image         0x85f00000 - 0x865bffff  (  6912 KB)
linux         0x87000000 - 0x8747ffff  (  4608 KB)

nvram         0x00000000 - 0x000fffff  (     1 MB)  RO
------------------------------------------------------
bootloader    0x00000000 - 0x0000ffff  (    64 KB)
permnv        0x00010000 - 0x0001ffff  (    64 KB)
dynnv         0x00020000 - 0x000fffff  (   896 KB)

flash         0x00000000 - 0x03ffffff  (    64 MB)  RO
------------------------------------------------------
linuxapps     0x00000000 - 0x019bffff  ( 26368 KB)
image1        0x019c0000 - 0x0207ffff  (  6912 KB)
image2        0x02080000 - 0x0273ffff  (  6912 KB)
linux         0x02740000 - 0x02bbffff  (  4608 KB)
linuxkfs      0x02bc0000 - 0x03dbffff  (    18 MB)
dhtml         0x03dc0000 - 0x03ffffff  (  2304 KB)
```

Dump partition `image1` from `flash` to `image.bin`, via the modem's
builtin telnet server at `192.168.100.1`, username `foo`, password `bar`.

```
$ ./bcm2dump dump 192.168.100.1,foo,bar flash image1 image.bin 
detected profile tc7200 (bfc)
dumping flash:0x019c0000-0x0207ffff
   3.13% (0x019f6000) 38944|29259 bytes/s (ETA      00:03:54)
...
```

Dump 128 kilobytes of RAM at `0x80004000` to `ramdump.bin`, using serial
over tcp, with the server at at `192.168.0.3:5555`.
```
$ bcm2dump dump 192.168.0.3,5555 ram 0x80004000,128k ramdump.bin
```

Dump 16 kilobytes of partition `dynnv` from `nvram` to `ramdump.bin`, starting
at offset `0x200`, using a serial console:
```
$ bcm2dump dump /dev/ttyUSB0 nvram dynnv+0x200,16k ramdump.bin
```


## bcm2cfg

This utility can be used to inspect, and modify device configuration data.
Supported formats are the `GatewaySettings.bin` file, as well as NVRAM
dumps (`permnv` / `nvram`).

Given a device profile, it can also be used to enrypt, decrypt, verify,
and fix a `GatewaySettings.bin` file. Dumping an unencrypted file does
not require a profile.

##### Usage

*Under construction*

```
Usage: bcm2cfg [<options>] <command> [<arguments> ...]

Options: 
  -P <profile>     Force profile
  -p <password>    Encryption password
  -k <key>         Encryption key (hex string)
  -f <format>      Input file format (auto/gws/dyn/perm)
  -q               Decrease verbosity
  -v               Increase verbosity

Commands: 
  verify  <infile>
  fix     <infile> [<outfile>]
  decrypt <infile> [<outfile>]
  encrypt <infile> [<outfile>]
  list    <infile> [<name>]
  get     <infile> [<name>]
  set     <infile> <name> <value> [<outfile>]
  dump    <infile> [<name>]
  info    <infile>
  help
```


# Writing a device profile

A device profile is neccessary for most functions to work as advertised.
All current definitions can be found in [profiledef.c](profiledef.c).

If the device's bootloader serial console has been disabled, and you do
not have access to the firmware console (either via serial connection,
or telnet), there are ways to enable them (coming soon).

The following information is required to add a new profile:

##### Firmware (if unlocked)

* Firmware image
* Output of `/flash/show` command
* Output of `/flash/help open`
* Output of `/version` command

To get the firmware image, dump either `image1` or `image2`.

```
$ bcm2dump -P generic /dev/ttyUSB0 flash image2 image.bin
```

##### Bootloader (if unlocked)

* Bootloader image (see below)
* Output of `p` command (partition table)

An easy way to locate the bootloader is to jump to an arbitrary location in RAM,
and then study the exception handler's output. Jumping to a random address is
one way to crash your device, but to be safe, you could write an opcode to RAM
that will cause a crash, and then jump to that location. Something
like `sw $zero, 0($zero)` (`0xac000000`) is always a safe bet:

```
w

Write memory.  Hex address: 0x80000000
Hex value: 0xac000000

j

Jump to arbitrary address (hex): 0x80000000

******************** CRASH ********************

EXCEPTION TYPE: 3/TLB (store)
TP0
r00/00 = 00000000 r01/at = 83f90000 r02/v0 = 80000000 r03/v1 = 00000001 
r04/a0 = 83f8e3c0 r05/a1 = 00000000 r06/a2 = 80000000 r07/a3 = 00000000 
r08/t0 = 00000020 r09/t1 = 00000000 r10/t2 = 00000029 r11/t3 = 0000003a 
r12/t4 = 20000000 r13/t5 = 000000a8 r14/t6 = 00000000 r15/t7 = 00000000 
r16/s0 = 942100d8 r17/s1 = 00000000 r18/s2 = 1dcd6500 r19/s3 = 0337f980 
r20/s4 = 94210084 r21/s5 = 000063d8 r22/s6 = efa9fd7c r23/s7 = 0000fc14 
r24/t8 = 00000002 r25/t9 = 00001021 r26/k0 = efa9fd7c r27/k1 = 83f8b16c 
r28/gp = 35552b87 r29/sp = 87ffff40 r30/fp = 00000215 r31/ra = 83f86fd0 

pc   : 0x80000000               sr  : 0x00000002
cause: 0x0000800c               addr: 0x00000000
```

The most important info here is `ra`, but we can also see many other
references to `0x83f8XXXX`, so it's safe to assume that the bootloader is
loaded somewhere around this address.

Restart the device, go into the main menu again, and we can fire up
`bcm2dump` to dump the bootloader code from ram. The bootloader is usually
very small, around 64k. To be safe, we'll dump 128k before and after
`0x83f80000`:

```
$ bcm2dump -P generic dump dev/ttyUSB0 0x83f60000,256k bootloader.bin
```

####### Flash read functions

Determining which function is used to read from flash might be more
difficult. In general, you're looking for a function that takes 3 parameters:
buffer, offset, and length. The dump code used by `bcm2dump` currently
supports 3 function signatures:

```
BCM2_READ_FUNC_PBOL: read(char **buffer, uint32_t offset, uint32_t length)
BCM2_READ_FUNC_BOL:  read(char *buffer, uint32_t offset, uint32_t length)
BCM2_READ_FUNC_OBL:  read(uint32_t offset, char *buffer, uint32_t length)
```

... and 4 return type definitions (which are currently ignored):

```
BCM2_RET_VOID: guess what?
BCM2_RET_OK_0: function returns zero on success
BCM2_RET_ERR_0: function returns zero on error
BCM2_RET_OK_LEN: function returns length on success
```

Use a string from the bootloader code as the profile's magic to support
profile auto-detection.



