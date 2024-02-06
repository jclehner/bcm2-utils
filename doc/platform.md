BCM33xx
=======

The BCM33xx chipset, sometimes also referred to as BCM933xx, is a cable modem SoC.

An overview of the different SoCs can be found on [deviwiki](https://deviwiki.com/wiki/Broadcom#tab=SoC).

The chipsets can be divided into 3 different categories, depending on their CPUs.

##### BCM3382 and older

These use a single core big-endian MIPS processor. The CM firmware is based on eCos.

##### BCM338x and BCM3384

The SoC includes two big-endian MIPS processors, sharing the same RAM space.

* "Viper" (arch `bmips4350`)
* "Zephyr" (arch `bmips5000`), aka "application processor"

Communication between both processors is handled by various IPC mechanisms.

The boot processor is Viper, which also runs the CM firmware (based on eCos). Once
the CM system has been intialized, it *may* boot Linux on the "application processor".

On these platforms, Linux is only used for providing things such as sharing attached
USB drives via Samba. All cable network and Wi-Fi related aspects (including the web
interface) are handled by the eCos firmware - the Linux part isn't needed for the device
to work as a basic modem/router.

Communication between both processors is handled by various IPC mechanisms.

##### BCM3390

This SoC includes two processors of *differing endianness*.

* ARM, "Brahma B15", (dual core, ARMv7, Cortex-A15), little-endian
* MIPS, big-endian

The boot processor is the ARM core. The device can be set to secure boot mode
using OTP (one-time programmable) memory.

The bootloader eventually boots Linux, which in turn starts a service that loads
the CM firmware into RAM, and boots the MIPS processor.

Linux handles networking, Wi-Fi, and also hosts the web interface. The CM firmware
(still based on eCos) only handles access to the cable network and telephony.

Communication between both cores is handled by IPC mechanisms and virtual
network interfaces.
