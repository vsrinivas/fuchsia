// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TEST_PLATFORM_DEVICE_H
#define TEST_PLATFORM_DEVICE_H

#include "platform_pci_device.h"
#include <memory>

class TestPlatformPciDevice {
public:
    static magma::PlatformPciDevice* GetInstance() { return g_instance.get(); }

    static void SetInstance(std::unique_ptr<magma::PlatformPciDevice> platform_device)
    {
        g_instance = std::move(platform_device);
    }

    static bool is_intel_gen(uint16_t device_id)
    {
        switch (device_id) {
            case 0x1916: // Intel(R) HD Graphics 520 (Skylake GT2)
            case 0x191E: // Intel(R) HD Graphics 515 (Skylake GT2)
            case 0x193b: // Intel(R) Iris Pro Graphics 580 (Skylake GT4e)
            case 0x5916: // Intel(R) HD Graphics 620 (Kabylake GT2)
            case 0x591E: // Intel(R) HD Graphics 615 (Kabylake GT2)
            case 0x5926: // Intel(R) Iris Graphics 640 (Kabylake GT3e)
            case 0x5927: // Intel(R) Iris Graphics 650 (Kabylake GT3e)
              return true;
        }
        return false;
    }

    static std::unique_ptr<magma::PlatformPciDevice> g_instance;
};

#endif // TEST_PLATFORM_DEVICE_H
