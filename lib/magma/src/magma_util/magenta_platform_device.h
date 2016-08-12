// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAGENTA_PLATFORM_DEVICE_H
#define MAGENTA_PLATFORM_DEVICE_H

#include "platform_device.h"

#include <ddk/device.h>

namespace magma {

class MagentaPlatformDevice : public PlatformDevice {
public:
    MagentaPlatformDevice(mx_device_t* mx_device) : mx_device_(mx_device) {}

    void* GetDeviceHandle() override { return mx_device(); }

    std::unique_ptr<PlatformMmio> CpuMapPciMmio(unsigned int pci_bar,
                                                PlatformMmio::CachePolicy cache_policy) override;

private:
    mx_device_t* mx_device() { return mx_device_; }

    mx_device_t* mx_device_;
};

} // namespace

#endif // MAGENTA_PLATFORM_DEVICE_H
