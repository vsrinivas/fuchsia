// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "sata.h"

#define GPT_MAGIC (0x5452415020494645ull) // 'EFI PART'
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
    uint8_t guid[16];
    uint64_t entries;
    uint32_t entries_count;
    uint32_t entries_sz;
    uint32_t entries_crc;
    uint8_t reserved[0]; // for the rest of the block
} gpt_t;

typedef struct gpt_entry {
    uint8_t type[16];
    uint8_t guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t flags;
    uint8_t name[72];
} gpt_entry_t;

typedef struct gpt_part_device {
    mx_device_t device;
    sata_device_t* disk;
    gpt_entry_t gpt_entry;
} gpt_partdev_t;

#define get_gpt_device(dev) containerof(dev, gpt_partdev_t, device)

mx_status_t gpt_bind(mx_driver_t* drv, mx_device_t* dev, sata_device_t* disk);
