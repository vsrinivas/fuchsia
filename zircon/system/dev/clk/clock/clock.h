// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/clock.h>
#include <ddktl/protocol/clockimpl.h>

namespace clock {

class ClockDevice;
using ClockDeviceType = ddk::Device<ClockDevice, ddk::Unbindable>;

class ClockDevice : public ClockDeviceType,
                    public ddk::ClockProtocol<ClockDevice, ddk::base_protocol> {
public:
    ClockDevice(zx_device_t* parent, clock_impl_protocol_t* clock, uint32_t id)
        : ClockDeviceType(parent), clock_(clock), id_(id) {}

    static zx_status_t Create(void* ctx, zx_device_t* parent);

    void DdkUnbind();
    void DdkRelease();

    zx_status_t ClockEnable();
    zx_status_t ClockDisable();

private:
    const ddk::ClockImplProtocolClient clock_;
    const uint32_t id_;
};

} // namespace clock
