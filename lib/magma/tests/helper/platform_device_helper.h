// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TEST_PLATFORM_DEVICE_H
#define TEST_PLATFORM_DEVICE_H

#include "platform_device.h"
#include <memory>

class TestPlatformDevice {
public:
    static magma::PlatformDevice* GetInstance() { return g_instance.get(); }

    static void SetInstance(std::unique_ptr<magma::PlatformDevice> platform_device)
    {
        g_instance = std::move(platform_device);
    }

    static bool is_intel_gen(uint16_t device_id)
    {
        switch (device_id) {
            case 0x1916: // Intel(R) HD Graphics 520 (Skylake GT2)
            case 0x5916: // Intel(R) HD Graphics 620 (Kabylake GT2)
            case 0x193b: // Intel(R) Iris Pro Graphics 580 (Skylake GT4e)
                return true;
        }
        return false;
    }

    static std::unique_ptr<magma::PlatformDevice> g_instance;
};

#endif // TEST_PLATFORM_DEVICE_H
