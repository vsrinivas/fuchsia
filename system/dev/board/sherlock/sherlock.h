// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/platform-bus.h>
#include <fbl/macros.h>

namespace sherlock {

class Sherlock;
using SherlockType = ddk::Device<Sherlock>;

// This is the main class for the platform bus driver.
class Sherlock : public SherlockType {
public:
    explicit Sherlock(zx_device_t* parent, platform_bus_protocol_t* pbus)
        : SherlockType(parent), pbus_(pbus) {}

    static zx_status_t Create(zx_device_t* parent);

    // Device protocol implementation.
    void DdkRelease();

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Sherlock);

    void Start();

    ddk::PlatformBusProtocolProxy pbus_;
};

} // namespace sherlock

__BEGIN_CDECLS
zx_status_t sherlock_bind(void* ctx, zx_device_t* parent);
__END_CDECLS
