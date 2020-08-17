// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_HW_GPT_H_
#define SYSROOT_ZIRCON_HW_GPT_H_

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/compiler.h>

#define GPT_MAGIC (0x5452415020494645ull)  // 'EFI PART'
#define GPT_HEADER_SIZE 0x5c
#define GPT_ENTRY_SIZE 0x80
#define GPT_GUID_LEN 16
#define GPT_GUID_STRLEN 37
#define GPT_NAME_LEN 72

typedef struct gpt_header {
  uint64_t magic;              // Magic number.
  uint32_t revision;           // Revision.
  uint32_t size;               // Size of the header.
  uint32_t crc32;              // Checksum of this header.
  uint32_t reserved0;          // Reserved field.
  uint64_t current;            // Block where this table is stored.
  uint64_t backup;             // Block where other copy of partition table is stored.
  uint64_t first;              // First usable block. Block after primary partition table ends.
  uint64_t last;               // Last usable block. Block before backup partition table starts.
  uint8_t guid[GPT_GUID_LEN];  // Disk GUID.
  uint64_t entries;            // Starting block where entries for this partition tables are found.
                               // Value equals 2 for primary copy.
  uint32_t entries_count;      // Total number of entries.
  uint32_t entries_size;       // Size of each entry.
  uint32_t entries_crc;        // Checksum of the entire entries array.
} __PACKED gpt_header_t;

static_assert(GPT_HEADER_SIZE == sizeof(gpt_header_t), "Gpt header size invalid");

typedef struct gpt_entry {
  uint8_t type[GPT_GUID_LEN];
  uint8_t guid[GPT_GUID_LEN];
  uint64_t first;
  uint64_t last;
  uint64_t flags;
  uint8_t name[GPT_NAME_LEN];  // UTF-16 on disk
} gpt_entry_t;

static_assert(GPT_ENTRY_SIZE == sizeof(gpt_entry_t), "Gpt entry size invalid");

// GUIDs are specified in mixed-endian, to avoid manual errors use this macro.
// Example usage: GPT_GUID(0x00112233, 0x4455, 0x6677, 0x8899, 0xAABBCCDDEEFF)
// clang-format off
#define GPT_GUID(group0, group1, group2, group3, group4) { \
  /* group0: 4 bytes, little-endian. */                    \
  (group0 >> 0) & 0xFF,                                    \
  (group0 >> 8) & 0xFF,                                    \
  (group0 >> 16) & 0xFF,                                   \
  (group0 >> 24) & 0xFF,                                   \
  /* group1: 2 bytes, little-endian. */                    \
  (group1 >> 0) & 0xFF,                                    \
  (group1 >> 8) & 0xFF,                                    \
  /* group2: 2 bytes, little-endian. */                    \
  (group2 >> 0) & 0xFF,                                    \
  (group2 >> 8) & 0xFF,                                    \
  /* group3: 2 bytes, big-endian. */                       \
  (group3 >> 8) & 0xFF,                                    \
  (group3 >> 0) & 0xFF,                                    \
  /* group4: 6 bytes, big-endian. */                       \
  (group4 >> 40) & 0xFF,                                   \
  (group4 >> 32) & 0xFF,                                   \
  (group4 >> 24) & 0xFF,                                   \
  (group4 >> 16) & 0xFF,                                   \
  (group4 >> 8) & 0xFF,                                    \
  (group4 >> 0) & 0xFF                                     \
}
// clang-format on

// == GPT partition definitions ==
//
// These are some common partition definitions used across various boards.
// The general scheme is:
//   |type|: identical for slotted partitions, e.g. zircon_{a,b,r} will all
//           share the same type GUID
//   |guid|: unspecified and generally expected to be random
//   |name|: specific name for uniquely identifying partitions
//
// New boards should adopt this scheme when possible, but see below for a
// slightly different legacy scheme used by existing boards.

// clang-format off

// bootloader_{a,b,r}
//
// These partitions are optional and may be used to hold bootloader and/or
// other firmware images. The format is SoC-specific.
#define GPT_BOOTLOADER_A_NAME         "bootloader_a"
#define GPT_BOOTLOADER_B_NAME         "bootloader_b"
#define GPT_BOOTLOADER_R_NAME         "bootloader_r"
#define GPT_BOOTLOADER_ABR_TYPE_GUID  GPT_GUID(0xfe8a2634, 0x5e2e, 0x46ba, 0x99e3, 0x3a192091a350)

// durable
//
// This partition holds mutable data that must remain intact across factory
// reset. It differs from durable_boot only in that it is larger, ignored by
// bootloaders, and is expected to have a filesystem.
//
// This partition is expected to be written to by Fuchsia during normal
// operation. It is expected to be read by Fuchsia, but not by any bootloader
// or firmware. It is expected to have a filesystem with encryption built in.
// Use of this partition increases attack surface and should be minimized.
#define GPT_DURABLE_NAME              "durable"
#define GPT_DURABLE_TYPE_GUID         GPT_GUID(0xd9fd4535, 0x106c, 0x4cec, 0x8d37, 0xdfc020ca87cb)

// durable_boot
//
// This partition holds A/B/R metadata and other very small mutable data that
// must remain intact across factory reset. There is no filesystem and the
// content layout is fixed.
//
// This partition is expected to be written to by Fuchsia and the main
// bootloader during normal operation. It is expected to be read by bootloaders
// very early in boot. It has no encryption or integrity check built in. Use of
// this partition increases attack surface and should be minimized.
#define GPT_DURABLE_BOOT_NAME         "durable_boot"
#define GPT_DURABLE_BOOT_TYPE_GUID    GPT_GUID(0xa409e16b, 0x78aa, 0x4acc, 0x995c, 0x302352621a41)

// factory
//
// This partition holds factory-provisioned data used by the Fuchsia-based
// system and is read-only.
//
// It is expected that this partition is only written in the factory and has a
// simple file system. It is not encrypted, but is checked for integrity by
// Fuchsia. Bootloaders and firmware are expected to ignore this partition.
#define GPT_FACTORY_NAME              "factory"
#define GPT_FACTORY_TYPE_GUID         GPT_GUID(0xf95d940e, 0xcaba, 0x4578, 0x9b93, 0xbb6c90f29d3e)

// factory_boot
//
// This partition holds factory-provisioned data used by the bootloader and is
// read-only. It must be small enough to be loaded into memory and verified
// during boot.
//
// It is expected that this partition is only written in the factory and has a
// simple structured format, not a filesystem. It is not encrypted but is
// checked for integrity by the verified boot process. It is expected to be read
// only by the main bootloader, not by Fuchsia.
#define GPT_FACTORY_BOOT_NAME         "factory_boot"
#define GPT_FACTORY_BOOT_TYPE_GUID    GPT_GUID(0x10b8dbaa, 0xd2bf, 0x42a9, 0x98c6, 0xa7c5db3701e7)

// fvm
//
// This partition is owned by the Fuchsia Volume Manager. It will be used for
// both system and user data.
#define GPT_FVM_NAME                  "fvm"
#define GPT_FVM_TYPE_GUID             GPT_GUID(0x49fd7cb8, 0xdf15, 0x4e73, 0xb9d9, 0x992070127f0f)

// vbmeta_{a,b,r}
//
// These partitions each hold verified boot metadata for a particular A/B/R
// slot. The format is defined by libavb.
//
// These partitions are expected to be written in the factory and during an OTA
// update. They are expected to be read by the main bootloader and possibly by
// Fuchsia. They are not encrypted, but are checked for integrity as part of the
// verified boot process.
#define GPT_VBMETA_A_NAME             "vbmeta_a"
#define GPT_VBMETA_B_NAME             "vbmeta_b"
#define GPT_VBMETA_R_NAME             "vbmeta_r"
#define GPT_VBMETA_ABR_TYPE_GUID      GPT_GUID(0x421a8bfc, 0x85d9, 0x4d85, 0xacda, 0xb64eec0133e9)

// zircon_{a,b,r}
//
// These partitions each hold a complete Zircon boot image, including an
// embedded bootfs image, for a particular A/B/R slot.
//
// These partitions are expected to be written in the factory and during an OTA
// update. They are expected to be read only by the main bootloader. They are
// not encrypted but are checked for integrity as part of the verified boot
// process.
#define GPT_ZIRCON_A_NAME             "zircon_a"
#define GPT_ZIRCON_B_NAME             "zircon_b"
#define GPT_ZIRCON_R_NAME             "zircon_r"
#define GPT_ZIRCON_ABR_TYPE_GUID      GPT_GUID(0x9b37fff6, 0x2e58, 0x466a, 0x983a, 0xf7926d0b04e0)

// Microsoft basic data partition
//
// These partitions usually contain FAT filesystems. They are mounted by the fat
// implementation at //src/storage/fuchsia-fatfs.
// These partitions do not have an expected label.
#define GPT_MICROSOFT_BASIC_DATA_TYPE_GUID \
    GPT_GUID(0xebd0a0a2, 0xb9e5, 0x4433, 0x87c0, 0x68b6b72699c7)

// clang-format on

// == Legacy GPT partition definitions ==
//
// These definitions instead use the following scheme:
//   |type|: unique for each partition, e.g. zircon_{a,b,r} will each have their
//           own type GUID
//   |guid|: unspecified and generally expected to be random
//   |name|: specific name, can use this or |type| find an individual partition

// clang-format off
#define GUID_EMPTY_STRING "00000000-0000-0000-0000-000000000000"
#define GUID_EMPTY_VALUE {                         \
    0x00, 0x00, 0x00, 0x00,                        \
    0x00, 0x00,                                    \
    0x00, 0x00,                                    \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 \
}
#define GUID_EMPTY_NAME "empty"

#define GUID_EFI_STRING "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
#define GUID_EFI_VALUE {                           \
    0x28, 0x73, 0x2a, 0xc1,                        \
    0x1f, 0xf8,                                    \
    0xd2, 0x11,                                    \
    0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b \
}
#define GUID_EFI_NAME "fuchsia-esp"

// GUID for a system partition
#define GUID_SYSTEM_STRING "606B000B-B7C7-4653-A7D5-B737332C899D"
#define GUID_SYSTEM_VALUE {                        \
    0x0b, 0x00, 0x6b, 0x60,                        \
    0xc7, 0xb7,                                    \
    0x53, 0x46,                                    \
    0xa7, 0xd5, 0xb7, 0x37, 0x33, 0x2c, 0x89, 0x9d \
}
#define GUID_SYSTEM_NAME "fuchsia-system"

// GUID for a data partition
#define GUID_DATA_STRING "08185F0C-892D-428A-A789-DBEEC8F55E6A"
#define GUID_DATA_VALUE {                          \
    0x0c, 0x5f, 0x18, 0x08,                        \
    0x2d, 0x89,                                    \
    0x8a, 0x42,                                    \
    0xa7, 0x89, 0xdb, 0xee, 0xc8, 0xf5, 0x5e, 0x6a \
}
#define GUID_DATA_NAME "fuchsia-data"

// GUID for a installer partition
#define GUID_INSTALL_STRING "48435546-4953-2041-494E-5354414C4C52"
#define GUID_INSTALL_VALUE {                       \
    0x46, 0x55, 0x43, 0x48,                        \
    0x53, 0x49,                                    \
    0x41, 0x20,                                    \
    0x49, 0x4E, 0x53, 0x54, 0x41, 0x4C, 0x4C, 0x52 \
}
#define GUID_INSTALL_NAME "fuchsia-install"

#define GUID_BLOB_STRING "2967380E-134C-4CBB-B6DA-17E7CE1CA45D"
#define GUID_BLOB_VALUE {                          \
    0x0e, 0x38, 0x67, 0x29,                        \
    0x4c, 0x13,                                    \
    0xbb, 0x4c,                                    \
    0xb6, 0xda, 0x17, 0xe7, 0xce, 0x1c, 0xa4, 0x5d \
}
#define GUID_BLOB_NAME "fuchsia-blob"

#define GUID_FVM_STRING "41D0E340-57E3-954E-8C1E-17ECAC44CFF5"
#define GUID_FVM_VALUE {                           \
    0x40, 0xe3, 0xd0, 0x41,                        \
    0xe3, 0x57,                                    \
    0x4e, 0x95,                                    \
    0x8c, 0x1e, 0x17, 0xec, 0xac, 0x44, 0xcf, 0xf5 \
}
#define GUID_FVM_NAME "fuchsia-fvm"

#define GUID_ZIRCON_A_STRING "DE30CC86-1F4A-4A31-93C4-66F147D33E05"
#define GUID_ZIRCON_A_VALUE {                       \
    0x86, 0xcc, 0x30, 0xde,                         \
    0x4a, 0x1f,                                     \
    0x31, 0x4a,                                     \
    0x93, 0xc4, 0x66, 0xf1, 0x47, 0xd3, 0x3e, 0x05, \
}
#define GUID_ZIRCON_A_NAME "zircon-a"

#define GUID_ZIRCON_B_STRING "23CC04DF-C278-4CE7-8471-897D1A4BCDF7"
#define GUID_ZIRCON_B_VALUE {                      \
    0xdf, 0x04, 0xcc, 0x23,                        \
    0x78, 0xc2,                                    \
    0xe7, 0x4c,                                    \
    0x84, 0x71, 0x89, 0x7d, 0x1a, 0x4b, 0xcd, 0xf7 \
}
#define GUID_ZIRCON_B_NAME "zircon-b"

#define GUID_ZIRCON_R_STRING "A0E5CF57-2DEF-46BE-A80C-A2067C37CD49"
#define GUID_ZIRCON_R_VALUE {                      \
    0x57, 0xcf, 0xe5, 0xa0,                        \
    0xef, 0x2d,                                    \
    0xbe, 0x46,                                    \
    0xa8, 0x0c, 0xa2, 0x06, 0x7c, 0x37, 0xcd, 0x49 \
}
#define GUID_ZIRCON_R_NAME "zircon-r"

#define GUID_SYS_CONFIG_STRING "4E5E989E-4C86-11E8-A15B-480FCF35F8E6"
#define GUID_SYS_CONFIG_VALUE {                    \
    0x9e, 0x98, 0x5e, 0x4e,                        \
    0x86, 0x4c,                                    \
    0xe8, 0x11,                                    \
    0xa1, 0x5b, 0x48, 0x0f, 0xcf, 0x35, 0xf8, 0xe6 \
}
#define GUID_SYS_CONFIG_NAME "sys-config"

#define GUID_FACTORY_CONFIG_STRING "5A3A90BE-4C86-11E8-A15B-480FCF35F8E6"
#define GUID_FACTORY_CONFIG_VALUE {                \
    0xbe, 0x90, 0x3a, 0x5a,                        \
    0x86, 0x4c,                                    \
    0xe8, 0x11,                                    \
    0xa1, 0x5b, 0x48, 0x0f, 0xcf, 0x35, 0xf8, 0xe6 \
}
#define GUID_FACTORY_CONFIG_NAME "factory-config"

#define GUID_BOOTLOADER_STRING "5ECE94FE-4C86-11E8-A15B-480FCF35F8E6"
#define GUID_BOOTLOADER_VALUE {                    \
    0xfe, 0x94, 0xce, 0x5e,                        \
    0x86, 0x4c,                                    \
    0xe8, 0x11,                                    \
    0xa1, 0x5b, 0x48, 0x0f, 0xcf, 0x35, 0xf8, 0xe6 \
}
#define GUID_BOOTLOADER_NAME "bootloader"

#define GUID_TEST_STRING "8B94D043-30BE-4871-9DFA-D69556E8C1F3"
#define GUID_TEST_VALUE {                          \
    0x43, 0xD0, 0x94, 0x8b,                        \
    0xbe, 0x30,                                    \
    0x71, 0x48,                                    \
    0x9d, 0xfa, 0xd6, 0x95, 0x56, 0xe8, 0xc1, 0xf3 \
}
#define GUID_TEST_NAME "guid-test"

#define GUID_VBMETA_A_STRING "A13B4D9A-EC5F-11E8-97D8-6C3BE52705BF"
#define GUID_VBMETA_A_VALUE {                      \
    0x9a, 0x4d, 0x3b, 0xa1,                        \
    0x5f, 0xec,                                    \
    0xe8, 0x11,                                    \
    0x97, 0xd8, 0x6c, 0x3b, 0xe5, 0x27, 0x05, 0xbf \
}
#define GUID_VBMETA_A_NAME "vbmeta_a"

#define GUID_VBMETA_B_STRING "A288ABF2-EC5F-11E8-97D8-6C3BE52705BF"
#define GUID_VBMETA_B_VALUE {                      \
    0xf2, 0xab, 0x88, 0xa2,                        \
    0x5f, 0xec,                                    \
    0xe8, 0x11,                                    \
    0x97, 0xd8, 0x6c, 0x3b, 0xe5, 0x27, 0x05, 0xbf \
}
#define GUID_VBMETA_B_NAME "vbmeta_b"

#define GUID_VBMETA_R_STRING "6A2460C3-CD11-4E8B-80A8-12CCE268ED0A"
#define GUID_VBMETA_R_VALUE {                      \
    0xc3, 0x60, 0x24, 0x6a,                        \
    0x11, 0xcd,                                    \
    0x8b, 0x4e,                                    \
    0x80, 0xa8, 0x12, 0xcc, 0xe2, 0x68, 0xed, 0x0a \
}
#define GUID_VBMETA_R_NAME "vbmeta_r"

#define GUID_ABR_META_STRING "1D75395D-F2C6-476B-A8B7-45CC1C97B476"
#define GUID_ABR_META_VALUE {                      \
    0x5d, 0x39, 0x75, 0x1d,                        \
    0xc6, 0xf2,                                    \
    0x6b, 0x47,                                    \
    0xa8, 0xb7, 0x45, 0xcc, 0x1c, 0x97, 0xb4, 0x76 \
}
#define GUID_ABR_META_NAME "misc"

#define GUID_CROS_KERNEL_STRING "FE3A2A5D-4F32-41A7-B725-ACCC3285A309"
#define GUID_CROS_KERNEL_VALUE {                   \
    0x5d, 0x2a, 0x3a, 0xfe,                        \
    0x32, 0x4f,                                    \
    0xa7, 0x41,                                    \
    0xb7, 0x25, 0xac, 0xcc, 0x32, 0x85, 0xa3, 0x09 \
}
#define GUID_CROS_KERNEL_NAME "cros-kernel"

#define GUID_CROS_ROOTFS_STRING "3CB8E202-3B7E-47DD-8A3C-7FF2A13CFCEC"
#define GUID_CROS_ROOTFS_VALUE {                   \
    0x02, 0xe2, 0xb8, 0x3C,                        \
    0x7e, 0x3b,                                    \
    0xdd, 0x47,                                    \
    0x8a, 0x3c, 0x7f, 0xf2, 0xa1, 0x3c, 0xfc, 0xec \
}
#define GUID_CROS_ROOTFS_NAME "cros-rootfs"

#define GUID_CROS_RESERVED_STRING "2E0A753D-9E48-43B0-8337-B15192CB1B5E"
#define GUID_CROS_RESERVED_VALUE {                 \
    0x3d, 0x75, 0x0a, 0x2e,                        \
    0x48, 0x9e,                                    \
    0xb0, 0x43,                                    \
    0x83, 0x37, 0xb1, 0x51, 0x92, 0xcb, 0x1b, 0x5e \
}
#define GUID_CROS_RESERVED_NAME "cros-reserved"

#define GUID_CROS_FIRMWARE_STRING "CAB6E88E-ABF3-4102-A07A-D4BB9BE3C1D3"
#define GUID_CROS_FIRMWARE_VALUE {                 \
    0x8e, 0xe8, 0xb6, 0xca,                        \
    0xf3, 0xab,                                    \
    0x02, 0x41,                                    \
    0xa0, 0x7a, 0xd4, 0xbb, 0x9b, 0xe3, 0xc1, 0xd3 \
}
#define GUID_CROS_FIRMWARE_NAME "cros-firmware"

#define GUID_CROS_DATA_STRING "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7"
#define GUID_CROS_DATA_VALUE {                     \
    0xa2, 0xa0, 0xd0, 0xeb,                        \
    0xe5, 0xb9,                                    \
    0x33, 0x44,                                    \
    0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7 \
}
#define GUID_CROS_DATA_NAME "cros-data"

#define GUID_BIOS_STRING "21686148-6449-6E6F-744E-656564454649"
#define GUID_BIOS_VALUE {                          \
    0x48, 0x61, 0x68, 0x21,                        \
    0x49, 0x64,                                    \
    0x6f, 0x6e,                                    \
    0x74, 0x4e, 0x65, 0x65, 0x64, 0x45, 0x46, 0x49 \
}
#define GUID_BIOS_NAME "bios"

#define GUID_EMMC_BOOT1_STRING "900B0FC5-90CD-4D4F-84F9-9F8ED579DB88"
#define GUID_EMMC_BOOT1_VALUE {                    \
    0xc5, 0x0f, 0x0b, 0x90,                        \
    0xcd, 0x90,                                    \
    0x4f, 0x4d,                                    \
    0x84, 0xf9, 0x9f, 0x8e, 0xd5, 0x79, 0xdb, 0x88 \
}
#define GUID_EMMC_BOOT1_NAME "emmc-boot1"

#define GUID_EMMC_BOOT2_STRING "B2B2E8D1-7C10-4EBC-A2D0-4614568260AD"
#define GUID_EMMC_BOOT2_VALUE {                    \
    0xd1, 0xe8, 0xb2, 0xb2,                        \
    0x10, 0x7c,                                    \
    0xbc, 0x4e,                                    \
    0xa2, 0xd0, 0x46, 0x14, 0x56, 0x82, 0x60, 0xad \
}
#define GUID_EMMC_BOOT2_NAME "emmc-boot2"

#define GUID_LINUX_FILESYSTEM_DATA_STRING "0FC63DAF-8483-4772-8E79-3D69D8477DE4"
#define GUID_LINUX_FILESYSTEM_DATA_VALUE {         \
    0xaf, 0x3d, 0xc6, 0x0f,                        \
    0x83, 0x84,                                    \
    0x72, 0x47,                                    \
    0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4 \
}
#define GUID_LINUX_FILESYSTEM_DATA_NAME "linux-filesystem"

// clang-format on

#endif  // SYSROOT_ZIRCON_HW_GPT_H_
