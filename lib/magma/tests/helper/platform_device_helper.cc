// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_device_helper.h"

std::unique_ptr<magma::PlatformPciDevice> TestPlatformPciDevice::g_instance;

std::unique_ptr<magma::PlatformDevice> TestPlatformDevice::g_instance;

void* GetTestDeviceHandle()
{
    magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
    if (platform_device)
        return platform_device->GetDeviceHandle();
    magma::PlatformPciDevice* platform_pci_device = TestPlatformPciDevice::GetInstance();
    if (platform_pci_device)
        return platform_pci_device->GetDeviceHandle();

    DLOG("no platform device found");
    return nullptr;
}
