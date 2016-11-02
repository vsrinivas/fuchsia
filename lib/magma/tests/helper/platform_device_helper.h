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

    static std::unique_ptr<magma::PlatformDevice> g_instance;
};

#endif // TEST_PLATFORM_DEVICE_H
