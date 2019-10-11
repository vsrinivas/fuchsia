// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_CLK_SYN_CLK_SYN_CLK_H_
#define ZIRCON_SYSTEM_DEV_CLK_SYN_CLK_SYN_CLK_H_

#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <ddktl/device.h>
#include <ddktl/protocol/clockimpl.h>
#include <fbl/mutex.h>
#include <soc/as370/as370-clk.h>

namespace clk {

class SynClk;
using DeviceType = ddk::Device<SynClk, ddk::UnbindableNew>;

class SynClk : public DeviceType, public ddk::ClockImplProtocol<SynClk, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Clock Protocol Implementation
  zx_status_t ClockImplEnable(uint32_t index);
  zx_status_t ClockImplDisable(uint32_t index);
  zx_status_t ClockImplIsEnabled(uint32_t id, bool* out_enabled);

  zx_status_t ClockImplSetRate(uint32_t id, uint64_t hz);
  zx_status_t ClockImplQuerySupportedRate(uint32_t id, uint64_t max_rate, uint64_t* out_best_rate);
  zx_status_t ClockImplGetRate(uint32_t id, uint64_t* out_current_rate);

  zx_status_t ClockImplSetInput(uint32_t id, uint32_t idx);
  zx_status_t ClockImplGetNumInputs(uint32_t id, uint32_t* out);
  zx_status_t ClockImplGetInput(uint32_t id, uint32_t* out);

  // Device Protocol Implementation.
  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();

  // Protected for unit tests.
 protected:
  SynClk(zx_device_t* parent, ddk::MmioBuffer global_mmio, ddk::MmioBuffer avio_mmio,
         ddk::MmioBuffer cpu_mmio)
      : DeviceType(parent),
        global_mmio_(std::move(global_mmio)),
        avio_mmio_(std::move(avio_mmio)),
        cpu_mmio_(std::move(cpu_mmio)) {}

 private:
  zx_status_t AvpllClkEnable(bool avpll0, bool enable);
  zx_status_t AvpllSetRate(bool avpll0, uint64_t rate);
  zx_status_t CpuSetRate(uint64_t rate);

  fbl::Mutex lock_;
  ddk::MmioBuffer global_mmio_ TA_GUARDED(lock_);
  ddk::MmioBuffer avio_mmio_ TA_GUARDED(lock_);
  ddk::MmioBuffer cpu_mmio_ TA_GUARDED(lock_);
};

}  // namespace clk

#endif  // ZIRCON_SYSTEM_DEV_CLK_SYN_CLK_SYN_CLK_H_
