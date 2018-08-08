// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/platform-defs.h>

#include "qemu-test.h"
#include "qemu-virt.h"

// This file loads four platform device drivers to test the platform bus support
// for providing platform bus resources to children of platform devices.
// The "parent" driver runs as a top level platform device (that is,
// it is a direct child of the platform bus. It binds the "child-1" driver as a
// child device, and child-1 creates children for the "child-2" and "child-3" drivers.
// All four of these drivers use the platform device protocol to map a unique MMIO region.
// Unfortunately we do not have an automated test for this feature yet,
// but one can manually inspect the boot log in arm64 qemu to verify that all four of these
// drivers loaded successfully:
//
// [00001.420] 02290.02335> qemu_test_bind: qemu-test-parent
// [00001.440] 02290.02335> qemu_test_bind: qemu-test-child-1
// [00001.458] 02290.02335> qemu_test_bind: qemu-test-child-2
// [00001.465] 02290.02335> qemu_test_bind: qemu-test-child-3

static const pbus_mmio_t parent_mmios[] = {
    {
        .base = TEST_MMIO_1,
        .length = TEST_MMIO_1_SIZE,
    },
};

static const pbus_mmio_t child_1_mmios[] = {
    {
        .base = TEST_MMIO_2,
        .length = TEST_MMIO_2_SIZE,
    },
};

static const pbus_mmio_t child_2_mmios[] = {
    {
        .base = TEST_MMIO_3,
        .length = TEST_MMIO_3_SIZE,
    },
};

static const pbus_mmio_t child_3_mmios[] = {
    {
        .base = TEST_MMIO_4,
        .length = TEST_MMIO_4_SIZE,
    },
};

static const pbus_dev_t child_1_kids[] = {
    {
        // Resources for child-2
        .mmios = child_2_mmios,
        .mmio_count = countof(child_2_mmios),
    },
    {
        // Resources for child-3
        .mmios = child_3_mmios,
        .mmio_count = countof(child_3_mmios),
    },
};

static const pbus_dev_t parent_kids[] = {
    {
        // Resources for child-1
        .mmios = child_1_mmios,
        .mmio_count = countof(child_1_mmios),
        .children = child_1_kids,
        .child_count = countof(child_1_kids),
    },
};

const pbus_dev_t test_dev = {
    .name = "qemu-test-parent",
    .vid = PDEV_VID_QEMU,
    .pid = PDEV_PID_QEMU,
    .did = PDEV_DID_QEMU_TEST_PARENT,
    .mmios = parent_mmios,
    .mmio_count = countof(parent_mmios),
    .children = parent_kids,
    .child_count = countof(parent_kids),
};

zx_status_t qemu_test_init(platform_bus_protocol_t* pbus) {
    return pbus_device_add(pbus, &test_dev, 0);
}
