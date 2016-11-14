// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define GPT_MAGIC (0x5452415020494645ull) // 'EFI PART'
#define GPT_GUID_LEN 16
#define GPT_GUID_STRLEN 37
#define GPT_NAME_LEN 72

typedef struct gpt {
    uint64_t magic;
    uint32_t revision;
    uint32_t size;
    uint32_t crc32;
    uint32_t reserved0;
    uint64_t current;
    uint64_t backup;
    uint64_t first_lba;
    uint64_t last_lba;
    uint8_t guid[GPT_GUID_LEN];
    uint64_t entries;
    uint32_t entries_count;
    uint32_t entries_sz;
    uint32_t entries_crc;
    uint8_t reserved[0]; // for the rest of the block
} gpt_t;

typedef struct gpt_entry {
    uint8_t type[GPT_GUID_LEN];
    uint8_t guid[GPT_GUID_LEN];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t flags;
    uint8_t name[GPT_NAME_LEN];
} gpt_entry_t;
