// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper/platform_device_helper.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_device.h"
#include "platform_firmware_loader.h"
#include "platform_pci_device.h"
#include "gtest/gtest.h"

TEST(MagmaUtil, PlatformFirmwareLoader)
{
    magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
    if (!platform_device) {
        // zx_intel_gpu_core_protocol_t can't be used with load_firmware, so this code doesn't work
        // with PlatformPciDevice on Intel. It's not needed there anyway.
        return;
    }

    auto firmware_loader =
        magma::PlatformFirmwareLoader::Create(platform_device->GetDeviceHandle());
    EXPECT_NE(nullptr, firmware_loader.get());
    std::unique_ptr<magma::PlatformBuffer> buffer;
    uint64_t size;
    EXPECT_EQ(MAGMA_STATUS_OK,
              firmware_loader->LoadFirmware("test_firmware.txt", &buffer, &size).get());
    EXPECT_NE(nullptr, buffer.get());
    EXPECT_EQ(59u, size);
}
