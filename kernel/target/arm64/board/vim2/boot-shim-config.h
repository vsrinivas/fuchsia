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
        .length = 0x80000000, // 2GB
    },
    {
        .type = BOOTDATA_MEM_RANGE_PERIPHERAL,
        .paddr = 0xc0000000,
        .length = 0x20000000,
    },
    {
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0,
        .length = 0x001000000,
    },
    {
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x10000000,
        .length = 0x00200000,
    },
    {
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x05100000,
        .length = 0x2000000,
    },
    {
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x7300000,
        .length = 0x100000,
    },
    {
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x75000000,
        .length = 0x9000000,
    },
};
