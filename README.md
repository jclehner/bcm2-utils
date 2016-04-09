# bcm2-utils

**B**road**c**o**m** **c**able **m**odem utilities.

* `bcm2dump`: A utility to dump ram/flash via a serial console
* `bcm2cfg`: A utility to modify/encrypt/decrypt the configuration
   dump (aka `GatewaySettings.bin`).

These utilities have been tested with a Technicolor TC-7200, but it
should be easy to add support for other devices. Some pointers for
this process are detailed [below](#writing-a-device-profile).

These utilities are not yet stable - command line options are likely
to change. Bug reports are always welcome.

## bcm2dump

Before using this utility, make sure that you're in the bootloader's
main menu. The bootloader should have the following options:

```
Main Menu:
==========
...
  r) Read memory
  w) Write memory
  j) Jump to arbitrary address
...

```

Dumping flash requires the "write memory" and "jump to arbitrary address" options,
plus an appropriate device profile. Speed is around 4.4 kilobytes/s when using
115200 baud.

Without a device profile, or if only "read memory" is available, `bcm2dump`
can only be used to dump ram. Speed in that case is slower, around
500 byte/s on a 115200 baud line.

Firmware images are usually in Broadcom's ProgramStore format. Utilities for
extraction and compression are available from Broadcom (and GPLv3'd!):

https://github.com/Broadcom/aeolus/tree/master/ProgramStore

## bcm2cfg

This utility handles the `GatewaySettings.bin` file that is used on some
devices (e.g. Technicolor TC7200, Thomson TWG850, Thomson TWG870). Given
a device profile, it can be used to encrypt, decrypt, verify the
settings file. Dumping an unencrypted file also works without a device profile.

This utility is currently alpha-ish at best!

Encrypted files must be decrypted before dumping them.

# Usage
###### bcm2dump

Listing available profiles:
```
$ bcm2dump -L
generic           Generic Profile
tc7200            Technicolor TC-7200/TC-7200.U
```

Show device profile (and list partitions:
```
$ bcm2dump -P tc7200 -L
PROFILE 'tc7200': Technicolor TC-7200/TC-7200.U
======================================================
baudrate   115200
pssig      0xa825
cfg_md5key 544d4d5f544337323030000000000000

SPACE 'ram': 0x80000000-0x88000000 (128 M)
name------------------offset--------size--------------
bootloader        0x83f80000  0x00020000  (128 K)
image1/2          0x85f00000  0x006c0000  (6912 K)
linux             0x87000000  0x00480000  (4608 K)

SPACE 'spi': 0x00000000-0x00100000 (1 M) R
name------------------offset--------size--------------
bootloader        0x00000000  0x00010000  (64 K)
permnv            0x00010000  0x00010000  (64 K)
dynnv             0x00020000  0x000e0000  (896 K)

SPACE 'nand': 0x00000000-0x04000000 (64 M) R
name------------------offset--------size--------------
linuxapps         0x00000000  0x019c0000  (26368 K)
image1            0x019c0000  0x006c0000  (6912 K)
image2            0x02080000  0x006c0000  (6912 K)
linux             0x02740000  0x00480000  (4608 K)
linuxkfs          0x02bc0000  0x01200000  (18 M)
dhtml             0x03dc0000  0x00240000  (2304 K)
```

Dumping `image1` from `nand` to image1.bin:

```
$ bcm2dump dump -P tc7200 -d /dev/ttyUSB0 -a nand -f image1.bin -o image1
dump: nand 0x019c0000-0x0207ffff
dump: writing dump code (412 b) to ram at 0xa4010000
dump: 100.00% (0xa401019b)   12|  10 bytes/s (ELT      00:00:37)
dump:   0.67% (0x019cb800) 4656|1782 bytes/s (ETA      01:05:47)
...
```

Dumping 128 kilobytes from ram at `0x80004000`:
```
$ bcm2dump dump -P tc7200 -d /dev/ttyUSB0 -a ram -f foobar.bin -o 0x80004000 -n 128k
```

# Writing a device profile

A device profile is neccessary for most functions to work as advertised.
All current definitions can be found in [profile.c](profile.c).

To add a new device profile, you'll first have to get hold of the bootloader code.
An easy way to locate the bootloader is to jump to an arbitrary location in RAM,
and then study the exception handler's output. Jumping to a random address is
one way to crash your device, but to be safe, you could write an opcode to RAM
that will cause a crash, and then jump to that location. Something
like `sw $zero, 0($zero)` (`0xac000000`) is always a safe bet:

```
Write memory.  Hex address: 0x80000000
Hex value: 0xac000000
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
$ bcm2dump dump -d /dev/ttyUSB0 -o 0x83f80000-128k -n 256k -f bootloader.bin
dump: falling back to slow dump method
dump:   4.50% (0x83f62e18)  528| 525 bytes/s (ETA      00:07:57)
...
```

Load the image in your favorite disassembler and start digging. The
`printf` address should be very easy to find; combined with a suitable
suitable location to store the dump code (currently around 512 bytes),
you'll be able to dump ram at "full" speed (4.4 kilobyte/s). Note that
the dump code must be loaded at a 64k boundary.

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
profile auto-detection (coming soon).

`cfg_md5key` and `cfg_keyfun` aside, the profile can be completed by
studying the bootloader code. The `cfg_` stuff is used by `bcm2cfg`,
but requires reverse engineering the actual firmware.



