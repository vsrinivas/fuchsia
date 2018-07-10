// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/i2c.h>
#include <ddktl/device.h>

namespace tcs {

class Tcs3400Device;
using DeviceType = ddk::Device<Tcs3400Device, ddk::Unbindable, ddk::Readable>;

class Tcs3400Device : public DeviceType {
public:
    Tcs3400Device(zx_device_t* device)
        : DeviceType(device) {}

    zx_status_t Bind();
    int Thread();

    // Methods required by the ddk mixins
    zx_status_t DdkRead(void* buf, size_t count, zx_off_t off, size_t* actual);
    void DdkUnbind();
    void DdkRelease();

private:
    static constexpr uint32_t kI2cIndex = 0;
    i2c_protocol_t i2c_;
    thrd_t thread_;
};
}
