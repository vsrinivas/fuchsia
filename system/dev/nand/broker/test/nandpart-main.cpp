// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "parent.h"

#include <unittest/unittest.h>
#include <zircon/hw/gpt.h>

constexpr zircon_nand_Info kNandInfo = {
    .page_size = 4096,
    .pages_per_block = 4,
    .num_blocks = 5,
    .ecc_bits = 6,
    .oob_size = 4,
    .nand_class = zircon_nand_Class_PARTMAP,
    .partition_guid = {},
};

constexpr zircon_nand_PartitionMap kPartitionMap = {
    .device_guid = {},
    .padding = 0,
    .partition_count = 1,
    .partitions = {
        {
            .type_guid = GUID_TEST_VALUE,
            .unique_guid = {},
            .first_block = 0,
            .last_block = 4,
            .copy_count = 0,
            .copy_byte_offset = 0,
            .name = {'t', 'e', 's', 't'},
            .padding = 0,
            .hidden = false,
            .bbt = false,
        },
    },
};

// The test can operate over either a ram-nand, or a real device. The simplest
// way to control what's going on is to have a place outside the test framework
// that controls where to execute, as "creation / teardown" of the external
// device happens at the process level.
ParentDevice* g_parent_device_;

int main(int argc, char** argv) {
    ParentDevice::TestConfig config = {};
    config.info = kNandInfo;
    config.partition_map = kPartitionMap;

    ParentDevice parent(config);
    if (!parent.IsValid()) {
        printf("Unable to create ram-nand device\n");
        return -1;
    }

    // Construct path to nandpart partition.
    char path[PATH_MAX];
    strcpy(path, parent.Path());
    strcat(path, "/test");

    ParentDevice::TestConfig nandpart_config = {};
    nandpart_config.path = path;

    ParentDevice nandpart_parent(nandpart_config);
    if (!nandpart_parent.IsValid()) {
        printf("Unable to attach to device\n");
        return -1;
    }

    g_parent_device_ = &nandpart_parent;

    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
