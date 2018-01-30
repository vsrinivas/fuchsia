// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>
#include <stdint.h>

#define GPT_MAGIC (0x5452415020494645ull) // 'EFI PART'
#define GPT_HEADER_SIZE 0x5c
#define GPT_ENTRY_SIZE  0x80
#define GPT_GUID_LEN 16
#define GPT_GUID_STRLEN 37
#define GPT_NAME_LEN 72

typedef struct gpt_header {
    uint64_t magic;
    uint32_t revision;
    uint32_t size;
    uint32_t crc32;
    uint32_t reserved0;
    uint64_t current;
    uint64_t backup;
    uint64_t first;
    uint64_t last;
    uint8_t guid[GPT_GUID_LEN];
    uint64_t entries;
    uint32_t entries_count;
    uint32_t entries_size;
    uint32_t entries_crc;
} gpt_header_t;

typedef struct gpt_entry {
    uint8_t type[GPT_GUID_LEN];
    uint8_t guid[GPT_GUID_LEN];
    uint64_t first;
    uint64_t last;
    uint64_t flags;
    uint8_t name[GPT_NAME_LEN];  // UTF-16 on disk
} gpt_entry_t;

#define GUID_EMPTY_VALUE {                         \
    0x00, 0x00, 0x00, 0x00,                        \
    0x00, 0x00,                                    \
    0x00, 0x00,                                    \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 \
}

#define GUID_EFI_VALUE {                           \
    0x28, 0x73, 0x2a, 0xc1,                        \
    0x1f, 0xf8,                                    \
    0xd2, 0x11,                                    \
    0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b \
}

// GUID for a system partition
#define GUID_SYSTEM_STRING "606B000B-B7C7-4653-A7D5-B737332C899D"
#define GUID_SYSTEM_VALUE {                        \
    0x0b, 0x00, 0x6b, 0x60,                        \
    0xc7, 0xb7,                                    \
    0x53, 0x46,                                    \
    0xa7, 0xd5, 0xb7, 0x37, 0x33, 0x2c, 0x89, 0x9d \
}

// GUID for a data partition
#define GUID_DATA_STRING "08185F0C-892D-428A-A789-DBEEC8F55E6A"
#define GUID_DATA_VALUE {                          \
    0x0c, 0x5f, 0x18, 0x08,                        \
    0x2d, 0x89,                                    \
    0x8a, 0x42,                                    \
    0xa7, 0x89, 0xdb, 0xee, 0xc8, 0xf5, 0x5e, 0x6a \
}

// GUID for a installer partition
#define GUID_INSTALL_STRING "48435546-4953-2041-494E-5354414C4C52"
#define GUID_INSTALL_VALUE {                       \
    0x46, 0x55, 0x43, 0x48,                        \
    0x53, 0x49,                                    \
    0x41, 0x20,                                    \
    0x49, 0x4E, 0x53, 0x54, 0x41, 0x4C, 0x4C, 0x52 \
}

#define GUID_BLOB_STRING "2967380E-134C-4CBB-B6DA-17E7CE1CA45D"
#define GUID_BLOB_VALUE {                          \
    0x0e, 0x38, 0x67, 0x29,                        \
    0x4c, 0x13,                                    \
    0xbb, 0x4c,                                    \
    0xb6, 0xda, 0x17, 0xe7, 0xce, 0x1c, 0xa4, 0x5d \
}

#define GUID_FVM_STRING "41D0E340-57E3-954E-8C1E-17ECAC44CFF5"
#define GUID_FVM_VALUE {                           \
    0x40, 0xe3, 0xd0, 0x41,                        \
    0xe3, 0x57,                                    \
    0x4e, 0x95,                                    \
    0x8c, 0x1e, 0x17, 0xec, 0xac, 0x44, 0xcf, 0xf5 \
}

#define GUID_ZIRCON_A_STRING "DE30CC86-1F4A-4A31-93C4-66F147D33E05"
#define GUID_ZIRCON_A_VALUE { \
    0x86, 0xcc, 0x30, 0xde, \
    0x4a, 0x1f, \
    0x31, 0x4a, \
    0x93, 0xc4, 0x66, 0xf1, 0x47, 0xd3, 0x3e, 0x05, \
}

#define GUID_ZIRCON_B_STRING "23CC04DF-C278-4CE7-8471-897D1A4BCDF7"
#define GUID_ZIRCON_B_VALUE { \
    0xdf, 0x04, 0xcc, 0x23, \
    0x78, 0xc2, \
    0xe7, 0x4c, \
    0x84, 0x71, 0x89, 0x7d, 0x1a, 0x4b, 0xcd, 0xf7 \
}

#define GUID_ZIRCON_R_STRING "A0E5CF57-2DEF-46BE-A80C-A2067C37CD49"
#define GUID_ZIRCON_R_VALUE { \
    0x57, 0xcf, 0xe5, 0xa0, \
    0xef, 0x2d, \
    0xbe, 0x46, \
    0xa8, 0x0c, 0xa2, 0x06, 0x7c, 0x37, 0xcd, 0x49 \
}
