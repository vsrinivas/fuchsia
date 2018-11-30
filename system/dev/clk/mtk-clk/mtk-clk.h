// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/mmio.h>
#include <ddktl/protocol/clk.h>
#include <zircon/device/clk.h>

namespace clk {

class MtkClk;
using DeviceType = ddk::Device<MtkClk, ddk::Ioctlable>;

class MtkClk : public DeviceType, public ddk::ClkProtocol<MtkClk> {
public:
    static zx_status_t Create(zx_device_t* parent);

    void DdkRelease() { delete this; }

    zx_status_t Bind();

    zx_status_t ClkEnable(uint32_t index);
    zx_status_t ClkDisable(uint32_t index);

    zx_status_t DdkIoctl(uint32_t op, const void* in_buf,
                         size_t in_len, void* out_buf,
                         size_t out_len, size_t* out_actual);

private:
    MtkClk(zx_device_t* parent, ddk::MmioBuffer mmio)
        : DeviceType(parent), mmio_(std::move(mmio)) {}

    zx_status_t ClkMeasure(uint32_t clk, clk_freq_info_t* info);

    ddk::MmioBuffer mmio_;
};

}  // namespace clk
