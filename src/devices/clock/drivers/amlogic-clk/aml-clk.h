// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_CLOCK_DRIVERS_AMLOGIC_CLK_AML_CLK_H_
#define SRC_DEVICES_CLOCK_DRIVERS_AMLOGIC_CLK_AML_CLK_H_

#include <fuchsia/hardware/clock/c/fidl.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <optional>
#include <vector>

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/clockimpl.h>
#include <fbl/array.h>
#include <fbl/mutex.h>
#include <hwreg/mmio.h>
#include <soc/aml-s905d2/s905d2-hiu.h>

#include "aml-clk-blocks.h"

namespace ddk {
class PBusProtocolClient;
}

namespace amlogic_clock {

class MesonPllClock;
class MesonCpuClock;
class MesonRateClock;

class AmlClock;
using DeviceType = ddk::Device<AmlClock, ddk::Unbindable, ddk::Messageable>;

class AmlClock : public DeviceType, public ddk::ClockImplProtocol<AmlClock, ddk::base_protocol> {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlClock);
  AmlClock(zx_device_t* device, ddk::MmioBuffer hiu_mmio, ddk::MmioBuffer dosbus_mmio,
           std::optional<ddk::MmioBuffer> msr_mmio, uint32_t device_id);
  ~AmlClock();

  // Performs the object initialization.
  static zx_status_t Create(zx_device_t* device);

  // CLK protocol implementation.
  zx_status_t ClockImplEnable(uint32_t clk);
  zx_status_t ClockImplDisable(uint32_t clk);
  zx_status_t ClockImplIsEnabled(uint32_t id, bool* out_enabled);

  zx_status_t ClockImplSetRate(uint32_t id, uint64_t hz);
  zx_status_t ClockImplQuerySupportedRate(uint32_t id, uint64_t max_rate, uint64_t* out_best_rate);
  zx_status_t ClockImplGetRate(uint32_t id, uint64_t* out_current_rate);

  zx_status_t ClockImplSetInput(uint32_t id, uint32_t idx);
  zx_status_t ClockImplGetNumInputs(uint32_t id, uint32_t* out_num_inputs);
  zx_status_t ClockImplGetInput(uint32_t id, uint32_t* out_input);

  // CLK IOCTL implementation.
  zx_status_t ClkMeasure(uint32_t clk, fuchsia_hardware_clock_FrequencyInfo* info);
  uint32_t GetClkCount();

  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

  // Device protocol implementation.
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  void ShutDown();

  void Register(const ddk::PBusProtocolClient& pbus);

 private:
  // Toggle clocks enable bit.
  zx_status_t ClkToggle(uint32_t clk, const bool enable);
  // Clock measure helper API.
  zx_status_t ClkMeasureUtil(uint32_t clk, uint64_t* clk_freq);

  // Toggle enable bit for PLL clocks.
  zx_status_t ClkTogglePll(uint32_t clk, const bool enable);

  // Checks the preconditions for SetInput, GetNumInputs and GetInput and
  // returns ZX_OK if the preconditions are met.
  zx_status_t IsSupportedMux(const uint32_t id, const uint16_t supported_mask);

  // Find the MesonRateClock that corresponds to clk. If ZX_OK is returned
  // `out` is populated with a pointer to the target clock.
  zx_status_t GetMesonRateClock(const uint32_t clk, MesonRateClock** out);

  void InitHiu();

  // IO MMIO
  ddk::MmioBuffer hiu_mmio_;
  ddk::MmioBuffer dosbus_mmio_;
  std::optional<ddk::MmioBuffer> msr_mmio_;
  // Protects clock gate registers.
  // Clock gates.
  fbl::Mutex lock_;
  const meson_clk_gate_t* gates_ = nullptr;
  size_t gate_count_ = 0;

  // Clock muxes.
  const meson_clk_mux_t* muxes_ = nullptr;
  size_t mux_count_ = 0;

  // Cpu Clocks.
  std::vector<MesonCpuClock> cpu_clks_;

  aml_hiu_dev_t hiudev_;
  std::unique_ptr<MesonPllClock> pllclk_[HIU_PLL_COUNT];

  // Clock Table
  const char* const* clk_table_ = nullptr;
  size_t clk_table_count_ = 0;
  // MSR_CLK offsets/
  meson_clk_msr_t clk_msr_offsets_;
};

}  // namespace amlogic_clock

#endif  // SRC_DEVICES_CLOCK_DRIVERS_AMLOGIC_CLK_AML_CLK_H_
