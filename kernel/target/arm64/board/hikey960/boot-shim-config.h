// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define HAS_DEVICE_TREE 1

static const bootdata_cpu_config_t cpu_config = {
    .cluster_count = 2,
    .clusters = {
        {
            .cpu_count = 4,
        },
        {
            .cpu_count = 4,
        },
    },
};

static const bootdata_mem_range_t mem_config[] = {
    {
        .type = BOOTDATA_MEM_RANGE_RAM,
        .length = 0xc0000000, // 3GB
    },
    {
        .type = BOOTDATA_MEM_RANGE_PERIPHERAL,
        .paddr = 0xe8100000,
        .length = 0x17f00000,
    },
    {
        // memory to reserve to avoid stomping on bootloader data
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x00000000,
        .length = 0x00080000,
    },
    {
        // bl31
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x20200000,
        .length = 0x200000,
    },
    {
        // pstore
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x20a00000,
        .length = 0x100000,
    },
    {
        // lpmx-core
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x89b80000,
        .length = 0x100000,
    },
    {
        // lpmcu
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x89c80000,
        .length = 0x40000,
    },
};
