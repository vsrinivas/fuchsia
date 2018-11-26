// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/mmio.h>
#include <ddktl/protocol/clk.h>

namespace clk {

class MtkClk;
using DeviceType = ddk::Device<MtkClk>;

class MtkClk : public DeviceType, public ddk::ClkProtocol<MtkClk> {
public:
    static zx_status_t Create(zx_device_t* parent);

    void DdkRelease() { delete this; }

    zx_status_t ClkEnable(uint32_t index);
    zx_status_t ClkDisable(uint32_t index);

private:
    MtkClk(zx_device_t* parent, ddk::MmioBuffer mmio)
        : DeviceType(parent), mmio_(std::move(mmio)) {}

    ddk::MmioBuffer mmio_;
};

}  // namespace clk
