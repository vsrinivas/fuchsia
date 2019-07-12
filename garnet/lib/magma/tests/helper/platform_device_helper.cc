// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_device_helper.h"

magma::PlatformPciDevice* TestPlatformPciDevice::g_instance;
void* TestPlatformPciDevice::core_device_;
static void* driver_device_s;

std::unique_ptr<magma::PlatformDevice> TestPlatformDevice::g_instance;

void SetTestDeviceHandle(void* handle) { driver_device_s = handle; }

void* GetTestDeviceHandle() {
  if (!driver_device_s) {
    return DRETP(nullptr, "no platform device found");
  }
  return driver_device_s;
}
