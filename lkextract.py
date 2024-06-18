#!/usr/bin/env python3

# bcm2-utils
# Copyright (C) 2024 Joseph C. Lehner <joseph.c.lehner@gmail.com>
#
# bcm2-utils is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# bcm2-utils is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with bcm2-utils.  If not, see <http://www.gnu.org/licenses/>.

from Cryptodome.Cipher import AES
from Cryptodome.PublicKey import RSA
from pathlib import Path
import binascii
import cstruct
import struct
import sys
import abc

CODE_TYPES = {
    0x00: "Binary",
    0x01: "ST7109-R",
    0x01: "ST",
    0x02: "ST7109-I",
    0x02: "ST-IMG",
    0x03: "ST7109-F",
    0x03: "ST-ROM",
    0x08: "SMP863x",
    0x08: "Sigma",
    0x09: "SMP863xZ",
    0x09: "Sigma-Z",
    0x0a: "SMP863xK",
    0x0a: "Sigma-K",
    0x14: "BCM",
    0x15: "BCM-ROM",
    0x16: "BCM-MS",
    0x17: "BCM-ELF",
    0x18: "BCM-MS1",
    0x19: "BCM-MS2",
    0x1a: "BCM-MSN",
    0x1b: "BCM-PwrKey",
    0x1c: "BCM-LxLoader",
    0x1d: "BCM-G8",
    0x1e: "Intel",
    0x1e: "IA",
    0x1f: "Intel-BZ",
    0x1f: "IA-BZ",
    0x20: "ARM",
    0x21: "SigSao",
    0x22: "LaunchKit",
    0x23: "TV2Boot",
    0x23: "BCM-MS3",
    0x24: "DblSigSao",
    0x25: "BCM-ARM",
    0x26: "ST-ARM",
    0x27: "BCM-ARM-ELF",
    0x28: "TV3Boot",
    0x29: "MRVL-ARM",
    0x2a: "BinaryReloc",
    0x2b: "QualArm",
    0x2c: "Charter",
    0x32: "SG-Linux",
    0x32: "LinuxApp",
    0x33: "BC-Linux",
    0x34: "ST-Linux",
    0x35: "IA-Linux",
    0x37: "BC-ARM-Linux",
    0x38: "ST-ARM-Linux",
    0x38: "ST-ArmLinux",
    0x36: "TI-Linux",
    0xe6: "VP-Generic",
    0xe7: "VP-Banker",
    0xe8: "VP-Text",
    0xe8: "Text",
    0xe9: "VP-Cert",
    0xf0: "VP-SecFW",
    0xf1: "VP-LockProt",
    0xfa: "VP-Reserve",
    0xfb: "VP-ResProt",
    0xfc: "VP-Binary",
    0xfd: "VP-BinProt",
}

SIGN_TYPES = {
    0x00: "Unsigned",
    0x03: "SaSign",
    0x04: "Sigma",
    0x05: "Bcm",
    0x08: "PK",
    0x09: "SHA256",
    0x0a: "SaCrypto",
    0x0a: "Cisco",
    0x0b: "SBP",
    0x0c: "SSD",
    0x0d: "SSV",
    0x13: "Simple",
    0x14: "CMS",
    0x1e: "NDS",
    0x7f: "Pass",
}

KEY_DIR = None
OUT_DIR = None

def eprint(msg):
    print(msg, file=sys.stderr)

def nprint(msg):
    print(msg, end="")

class Container(metaclass=abc.ABCMeta):
    def __init__(self, filename, offset):
        self.filename = filename
        self.offset = offset
        self.header = self._create_header()

        with open(filename, 'rb') as f:
            f.seek(self.offset)
            self._read_header(f)

    def is_header_crc_valid(self):
        return self._is_header_crc_valid() if self.is_valid() else false

    def is_data_crc_valid(self):
        return self._is_data_crc_valid() if self.is_valid() else false

    def data_as_bytes(self):
        with open(self.filename, 'rb') as f:
            f.seek(self.offset + self.data_offset())
            return f.read(self.data_size())

    def data_as(self, container, offset=0):
        return container(self.filename, self.offset + self.data_offset() + offset)

    def data_offset(self):
        return self.header.size

    def _read_header(self, f):
        self.header.unpack(f.read(self.header.size))

    def _is_header_crc_valid(self):
        return true

    def _is_data_crc_valid(self):
        return true

    @abc.abstractmethod
    def data_size() -> int:
        pass

    @abc.abstractmethod
    def is_valid(self) -> bool:
        pass

    @abc.abstractmethod
    def _create_header(self) -> cstruct.MemCStruct:
        pass

class SaoFile(Container):
    class Header(cstruct.MemCStruct):
        __byte_order__ = cstruct.BIG_ENDIAN
        __def__ = """
            struct SaoHeader {
                char magic[4];
                uint32_t header_crc;
                char type[4];
                uint8_t version[4];
                uint32_t data_size;
                uint32_t data_size2;
                uint8_t unknown1;
                uint8_t code_type;
                uint8_t sign_type;
                uint8_t unknown2;
                uint32_t flash_addr;
                uint32_t flash_size;
                uint32_t load_addr;
                uint32_t start_offset;
                uint32_t stack_ptr;
                uint32_t unknown3;
                uint32_t data_crc;
                uint8_t flash_attr;
                uint8_t unknown4;
                uint8_t run_ram;
                uint8_t target_receiver;
                uint8_t banker_attr;
                uint8_t unknown5;
                uint8_t unknown6;
                uint8_t unknown7;
            };
        """

    def __init__(self, filename, offset=0, data=None):
        self.data = data
        super().__init__(filename, offset)

    def _read_header(self, f):
        if self.data is not None:
            self.header.unpack(self.data)
        else:
            super()._read_header(f)

    def code_type(self):
        ct = self.header.code_type
        try:
            return CODE_TYPES[ct]
        except KeyError:
            return hex(ct)

    def sign_type(self):
        st = self.header.sign_type

        if st & 0x80:
            ret = "Enc:"
            st ^= 0x80
        else:
            ret = ""

        try:
            ret += SIGN_TYPES[st];
        except KeyError:
            ret += hex(st)

        return ret


    def is_valid(self):
        return self.header.magic == b'SOBJ'

    def data_size(self):
        return self.header.data_size

    def _create_header(self):
        return SaoFile.Header()

    def _is_header_crc_valid(self):
        return self._calc_checksum() == self.header.header_crc

    def _is_data_crc_valid(self):
        return binascii.crc32(self.data()) == self.header.data_crc

    def _calc_checksum(self):
        data = self.header.pack()[8:]
        return binascii.crc32(data)

class LaunchKitContainer(Container):
    class Header(cstruct.MemCStruct):
        __byte_order__ = cstruct.BIG_ENDIAN
        __def__ = """
            struct LaunchKitHeader {
                uint32_t header_crc;
                char magic[4];
                uint32_t data_offset;
                uint32_t data_size;
            };
        """
    class Entry(cstruct.MemCStruct):
        __byte_order__ = cstruct.BIG_ENDIAN
        __def__ = """
            struct LaunchKitEntry {
                char type[4];
                uint32_t unknown1;
                uint32_t data_offset;
                uint32_t data_size;
            };
        """

    def __init__(self, filename, offset=0):
        self.entries = []
        self.__header_crc_valid = False
        super().__init__(filename, offset)

    def is_valid(self):
        return self.header.magic == b'LKit'

    def data_size(self):
        return self.header.data_size

    def data_offset(self):
        return self.header.data_offset

    def entry_as_sao(self, i):
        return SaoFile(self.filename, self.offset + self.entries[i].data_offset)

    def _create_header(self):
        return LaunchKitContainer.Header()

    def _read_header(self, f):
        super()._read_header(f)

        crc = binascii.crc32(self.header.pack()[4:])

        offset = self.header.size

        while offset < self.header.data_offset:
            entry = LaunchKitContainer.Entry(f)
            crc = binascii.crc32(entry.pack(), crc)

            if entry.type != b'\x00\x00\x00\x00':
                self.entries.append(entry)

            offset += entry.size

        # remainder, if any
        crc = binascii.crc32(f.read(self.header.data_offset - offset), crc)

        self.__is_header_crc_valid = (crc == self.header.header_crc)

    def _is_header_crc_valid(self):
        return self.__is_header_crc_valid

class DerData(Container):
    def __init__(self, filename, offset=0):
        super().__init__(filename, offset)

    def is_valid(self):
        return self.header[0] == 0x30 and self.header[1] == 0x82

    def data_offset(self):
        return 0

    def data_size(self):
        return ((self.header[2] << 8) | self.header[3]) + 4

    def _create_header(self):
        return None

    def _read_header(self, f):
        self.header = f.read(4)

class EncryptedContainer(Container):
    class Header(cstruct.MemCStruct):
        __byte_order__ = cstruct.LITTLE_ENDIAN
        __def__ = """
            struct EncryptedContainerHeader
            {
                uint32_t header_crc;
                char magic[4];
                uint32_t data_size;
                uint32_t data_crc;
                char key_id[4];
                uint8_t unknown[12];
                uint8_t key_blob[256];
            };
        """

    def __init__(self, filename, offset):
        super().__init__(filename, offset)

    def is_valid(self):
        return self.header.magic == b'ENCK'

    def data_size(self):
        return self.header.data_size

    def _create_header(self):
        return EncryptedContainer.Header()

    def _is_header_crc_valid(self):
        return binascii.crc32(self.header.pack()[4:]) == self.header.header_crc

    def _is_data_crc_valid(self):
        return binascii.crc32(self.data()) == self.header.data_crc

class PkecKeyBlob:
    def __init__(self, offset, data, rsa):
        dec = rsa._decrypt(int.from_bytes(data, 'big')).to_bytes()
        self.offset = offset
        self.__valid = False
        self.enc_type = 0xff
        self.iv = b''
        self.key = b''

        padding = b'\x01' + (205 * b'\xff') + b'\x00'

        if not dec.startswith(padding):
            return

        key_blob = dec.removeprefix(padding)

        if key_blob[0:4] != b'CEKP':
            return

        self.__valid = True
        self.enc_type = key_blob[15]
        self.iv = key_blob[16:32]
        self.key = key_blob[32:]

    def print(self, level):
        nprint("0x%08x %s PKEC  " % (self.offset, level * ' '))

        if self.__valid:
            nprint("encryption=")

            if self.is_aes128():
                if not self.is_ecb():
                    nprint("AES-128-CBC, iv=%s, " % self.iv.hex())
                else:
                    nprint("AES-128-ECB ")

                print("key=%s" % self.key.hex())
            else:
                print("(%02x)" % self.enc_type)
        else:
            print("(INVALID)")

    def decrypt(self, data):
        if not self.is_aes128():
            raise RuntimeError("unknown encryption method")

        if self.is_ecb():
            cipher = AES.new(self.key, AES.MODE_ECB)
        else:
            cipher = AES.new(self.key, AES.MODE_CBC, self.iv)

        return cipher.decrypt(data)

    def is_valid(self):
        return self.__valid

    def is_aes128(self):
        return self.enc_type < 2

    def is_ecb(self):
        return self.enc_type == 0

def asn1_sequence_len(data):
    if len(data) >= 4 and data[0] == 0x30 and data[1] == 0x82:
        return 4 + ((data[2] << 8) | data[3])
    else:
        raise ValueError("expected an ASN.1 sequence")

def dump_lk(lk, level=0):
    if not lk.is_valid():
        raise ValueError("expected a LaunchKit header")

    indent = level * " "

    nprint("0x%08x %s LKit  " % (lk.offset, indent))

    if lk.is_header_crc_valid():
        nprint("data_offset=%d, " % lk.header.data_offset)
        print("data_size=%d" % lk.header.data_size)

        for i in range(len(lk.entries)):
            dump_sao(lk.entry_as_sao(i), level + 1)
    else:
        print("(INVALID)")

def dump_der(der, level=0):
    nprint("0x%08x %s DER   " % (der.offset, level * " "))

    if der.is_valid():
        print("size=%d" % der.data_size())
        return True
    else:
        print("(INVALID)")
        return False

def dump_enck(enck, level=0):
    nprint("0x%08x %s ENCK  " % (enck.offset, level * " "))

    if enck.is_valid():
        key_id = enck.header.key_id.decode("ascii")[::-1]
        key_file = KEY_DIR / f"{key_id}.pem"

        nprint("size=%d, " % enck.header.data_size)
        nprint("key_id=%s" % key_id)

        if key_file.exists():
            print()
            rsa = RSA.import_key(key_file.read_bytes())
            kb = PkecKeyBlob(enck.offset + 0x20, enck.header.key_blob, rsa)
            kb.print(level + 1)

            if kb.is_valid():
                data = kb.decrypt(enck.data_as_bytes())
                sao = SaoFile(enck.filename, enck.offset + 0x120, data)
                dump_sao(sao, level + 2)

                if OUT_DIR is not None:
                    name = "%s_0x%08x_%s.bin" % (Path(sao.filename).name, sao.offset, sao.header.type.decode('ascii'))
                    (OUT_DIR / name).write_bytes(data[0:sao.header.size + sao.header.data_size])
            else:
                print(" (FAILED)")
        else:
            print(" (UNAVAILABLE)")

    else:
        print("(INVALID)")

def dump_sao(sao, level=0):
    if not sao.is_valid():
        raise ValueError("expected an SAO header")

    indent = level * " "

    nprint("0x%08x %s SAO   " % (sao.offset, indent))

    if sao.is_header_crc_valid():
        nprint("type=%s, " % sao.header.type.decode('ascii'))
        nprint("size=%d, " % sao.header.data_size)
        nprint("code_type=%s(0x%02x), " % (sao.code_type(), sao.header.code_type))
        nprint("sign_type=%s(0x%02x)" % (sao.sign_type(), sao.header.sign_type))

        if sao.header.load_addr != 0:
            print(", load_addr=0x%x" % (sao.header.load_addr))
        else:
            print()

        if sao.header.code_type == 0x22:
            dump_lk(sao.data_as(LaunchKitContainer), level + 1)
        elif sao.header.code_type == 0x21:
            der = sao.data_as(DerData)

            dump_der(der, level + 1)

            if der.is_valid() and sao.header.sign_type & 0x80:
                enck = sao.data_as(EncryptedContainer, der.data_size())
                dump_enck(enck, level + 1)
    else:
        print("(INVALID)")

def dump_sao_file(filename):
    der = DerData(filename)
    if der.is_valid():
        dump_der(der)
        sao = SaoFile(filename, der.data_size() + 3)
    else:
        sao = SaoFile(filename)

    dump_sao(sao)

def check_directory(argv, index, default):
    if index < len(argv):
        ret = Path(argv[index])
        if not ret.is_dir():
            raise ValueError(f"not a directory: {ret}")
        return ret
    else:
        return default

if len(sys.argv) < 2 or len(sys.argv) > 4:
    eprint("Usage: lkextract.py [input file] <[key directory]> <[output directory]>")
    eprint("")
    eprint("The key directory is expected to contain '<key_id>.pem' files (defaults to current dir)")
    exit(1)

KEY_DIR = check_directory(sys.argv, 2, Path.cwd())
OUT_DIR = check_directory(sys.argv, 3, None)

dump_sao_file(sys.argv[1])
