// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/clockimpl.h>
#include <fbl/mutex.h>
#include <lib/mmio/mmio.h>
#include <soc/as370/as370-clk.h>
#include <lib/zircon-internal/thread_annotations.h>

namespace clk {

class SynClk;
using DeviceType = ddk::Device<SynClk, ddk::Unbindable>;

class SynClk : public DeviceType,
               public ddk::ClockImplProtocol<SynClk, ddk::base_protocol> {
public:
    static zx_status_t Create(void* ctx, zx_device_t* parent);

    zx_status_t Init();

    // Clock Protocol Implementation
    zx_status_t ClockImplEnable(uint32_t index);
    zx_status_t ClockImplDisable(uint32_t index);
    zx_status_t ClockImplIsEnabled(uint32_t id, bool* out_enabled);

    zx_status_t ClockImplSetRate(uint32_t id, uint64_t hz);
    zx_status_t ClockImplQuerySupportedRate(uint32_t id, uint64_t max_rate, uint64_t* out_best_rate);
    zx_status_t ClockImplGetRate(uint32_t id, uint64_t* out_current_rate);

    // Device Protocol Implementation.
    zx_status_t Bind();
    void DdkUnbind();
    void DdkRelease();

// Protected for unit tests.
protected:
    SynClk(zx_device_t* parent, ddk::MmioBuffer global_mmio, ddk::MmioBuffer avio_mmio)
        : DeviceType(parent), global_mmio_(std::move(global_mmio)),
          avio_mmio_(std::move(avio_mmio)) {}

private:
    zx_status_t RegisterClockProtocol();
    zx_status_t AvpllClkEnable(bool avpll0, bool enable);
    zx_status_t AvpllSetRate(bool avpll0, uint32_t rate);

    fbl::Mutex lock_;
    ddk::MmioBuffer global_mmio_ TA_GUARDED(lock_);
    ddk::MmioBuffer avio_mmio_ TA_GUARDED(lock_);
};

} // namespace clk
