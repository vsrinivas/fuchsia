// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/clock.h>
#include <ddktl/protocol/clockimpl.h>
#include <fbl/array.h>

namespace clock {

class ClockDevice;
using ClockDeviceType = ddk::Device<ClockDevice, ddk::Unbindable>;

class ClockDevice : public ClockDeviceType,
                    public ddk::ClockProtocol<ClockDevice, ddk::base_protocol> {
public:
    ClockDevice(zx_device_t* parent, clock_impl_protocol_t* clock, fbl::Array<uint32_t> map)
        : ClockDeviceType(parent), clock_(clock), map_(std::move(map)) {}

    static zx_status_t Create(void* ctx, zx_device_t* parent);

    void DdkUnbind();
    void DdkRelease();

    zx_status_t ClockEnable(uint32_t index);
    zx_status_t ClockDisable(uint32_t index);

private:
    const ddk::ClockImplProtocolClient clock_;
    const fbl::Array<uint32_t> map_;
};

} // namespace clock