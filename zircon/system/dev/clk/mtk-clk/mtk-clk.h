// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/clockimpl.h>
#include <lib/mmio/mmio.h>
#include <fuchsia/hardware/clock/c/fidl.h>

namespace clk {

class MtkClk;
using DeviceType = ddk::Device<MtkClk, ddk::Messageable>;

class MtkClk : public DeviceType, public ddk::ClockImplProtocol<MtkClk, ddk::base_protocol> {
public:
    static zx_status_t Create(zx_device_t* parent);

    void DdkRelease() { delete this; }

    zx_status_t Bind();

    zx_status_t ClockImplEnable(uint32_t index);
    zx_status_t ClockImplDisable(uint32_t index);
    zx_status_t ClockImplRequestRate(uint32_t id, uint64_t hz);

    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

    zx_status_t ClkMeasure(uint32_t clk, fuchsia_hardware_clock_FrequencyInfo* info);
    uint32_t GetClkCount();

private:
    MtkClk(zx_device_t* parent, ddk::MmioBuffer mmio)
        : DeviceType(parent), mmio_(std::move(mmio)) {}

    ddk::MmioBuffer mmio_;
};

} // namespace clk

