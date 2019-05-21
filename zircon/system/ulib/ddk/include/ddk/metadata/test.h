// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_METADATA_TEST_H_
#define DDK_METADATA_TEST_H_

#include <zircon/types.h>

namespace board_test {

static constexpr size_t kNameLengthMax = 32;

// Describes metadata passed via ZBI to test board driver.

struct DeviceEntry {
    char name[kNameLengthMax];
    // BIND_PLATFORM_DEV_VID`
    uint32_t vid;
    // BIND_PLATFORM_DEV_PID`
    uint32_t pid;
    // BIND_PLATFORM_DEV_DID`
    uint32_t did;

    // Below metadata is passed on to the device in DEVICE_METADATA_TEST.
    size_t metadata_size;
    const uint8_t* metadata;
};

struct DeviceList {
    size_t count;
    DeviceEntry list[];
};

} // namespace board_test

#endif // DDK_METADATA_TEST_H_
