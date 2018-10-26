// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/gpio-impl.h>
#include <ddktl/device.h>
#include <ddktl/protocol/platform-bus.h>
#include <threads.h>

namespace imx8mmevk {

#define ERROR(fmt, ...) zxlogf(ERROR, "[%s %d] " fmt, __func__, __LINE__, ##__VA_ARGS__)

class Board;
using DeviceType = ddk::Device<Board>;

class Board : public DeviceType {
public:
    explicit Board(zx_device_t* parent, const pbus_protocol_t& pbus)
        : DeviceType(parent),
          pbus_(&pbus) {}
    static zx_status_t Create(zx_device_t* parent);
    void DdkRelease() { delete this; }

private:
    zx_status_t StartAll();
    zx_status_t StartGpio();
    int Thread();

    ddk::PBusProtocolProxy pbus_;
    gpio_impl_protocol_t gpio_impl_ = {};
    thrd_t thread_ = {};
};

} // namespace imx8mmevk
