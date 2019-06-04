// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <ddktl/device.h>
#include <ddktl/protocol/platform/bus.h>

namespace board_as370 {

class As370 : public ddk::Device<As370> {
public:
    As370(zx_device_t* parent, const ddk::PBusProtocolClient pbus)
        : ddk::Device<As370>(parent), pbus_(pbus) {}

    static zx_status_t Create(void* ctx, zx_device_t* parent);

    void DdkRelease() { delete this; }

private:
    zx_status_t Start();
    int Thread();

    zx_status_t GpioInit();
    zx_status_t I2cInit();

    ddk::PBusProtocolClient pbus_;
    thrd_t thread_;
};

} // namespace board_as370
