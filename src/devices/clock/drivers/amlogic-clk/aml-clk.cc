// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-clk.h"

#include <fuchsia/hardware/clock/c/fidl.h>
#include <lib/device-protocol/pdev.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/protocol/platform/bus.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <soc/aml-meson/aml-clk-common.h>

#include "aml-axg-blocks.h"
#include "aml-g12a-blocks.h"
#include "aml-g12b-blocks.h"
#include "aml-gxl-blocks.h"
#include "aml-sm1-blocks.h"

namespace amlogic_clock {

// MMIO Indexes
static constexpr uint32_t kHiuMmio = 0;
static constexpr uint32_t kDosbusMmio = 1;
static constexpr uint32_t kMsrMmio = 2;

#define MSR_WAIT_BUSY_RETRIES 5
#define MSR_WAIT_BUSY_TIMEOUT_US 10000

AmlClock::AmlClock(zx_device_t* device, ddk::MmioBuffer hiu_mmio, ddk::MmioBuffer dosbus_mmio,
                   std::optional<ddk::MmioBuffer> msr_mmio, uint32_t device_id)
    : DeviceType(device),
      hiu_mmio_(std::move(hiu_mmio)),
      dosbus_mmio_(std::move(dosbus_mmio)),
      msr_mmio_(std::move(msr_mmio)) {
  // Populate the correct register blocks.
  switch (device_id) {
    case PDEV_DID_AMLOGIC_AXG_CLK: {
      // Gauss
      gates_ = axg_clk_gates;
      gate_count_ = countof(axg_clk_gates);
      break;
    }
    case PDEV_DID_AMLOGIC_GXL_CLK: {
      gates_ = gxl_clk_gates;
      gate_count_ = countof(gxl_clk_gates);
      break;
    }
    case PDEV_DID_AMLOGIC_G12A_CLK: {
      // Astro
      clk_msr_offsets_ = g12a_clk_msr;

      clk_table_ = static_cast<const char* const*>(g12a_clk_table);
      clk_table_count_ = countof(g12a_clk_table);

      gates_ = g12a_clk_gates;
      gate_count_ = countof(g12a_clk_gates);

      InitHiu();

      break;
    }
    case PDEV_DID_AMLOGIC_G12B_CLK: {
      // Sherlock
      clk_msr_offsets_ = g12b_clk_msr;

      clk_table_ = static_cast<const char* const*>(g12b_clk_table);
      clk_table_count_ = countof(g12b_clk_table);

      gates_ = g12b_clk_gates;
      gate_count_ = countof(g12b_clk_gates);

      InitHiu();

      break;
    }
    case PDEV_DID_AMLOGIC_SM1_CLK: {
      // Nelson
      gates_ = sm1_clk_gates;
      gate_count_ = countof(sm1_clk_gates);

      muxes_ = sm1_muxes;
      mux_count_ = countof(sm1_muxes);

      InitHiu();

      break;
    }
    default:
      ZX_PANIC("aml-clk: Unsupported SOC DID %u\n", device_id);
  }
}

zx_status_t AmlClock::Create(zx_device_t* parent) {
  zx_status_t status;

  // Get the platform device protocol and try to map all the MMIO regions.
  ddk::PDev pdev(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "aml-clk: failed to get pdev protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  std::optional<ddk::MmioBuffer> hiu_mmio = std::nullopt;
  std::optional<ddk::MmioBuffer> dosbus_mmio = std::nullopt;
  std::optional<ddk::MmioBuffer> msr_mmio = std::nullopt;

  // All AML clocks have HIU and dosbus regs but only some support MSR regs.
  // Figure out which of the varieties we're dealing with.
  status = pdev.MapMmio(kHiuMmio, &hiu_mmio);
  if (status != ZX_OK || !hiu_mmio) {
    zxlogf(ERROR, "aml-clk: failed to map HIU regs, status = %d", status);
    return status;
  }

  status = pdev.MapMmio(kDosbusMmio, &dosbus_mmio);
  if (status != ZX_OK || !dosbus_mmio) {
    zxlogf(ERROR, "aml-clk: failed to map DOS regs, status = %d", status);
    return status;
  }

  // Use the Pdev Device Info to determine if we've been provided with two
  // MMIO regions.
  pdev_device_info_t info;
  status = pdev.GetDeviceInfo(&info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-clk: failed to get pdev device info, status = %d", status);
    return status;
  }

  if (info.mmio_count > kMsrMmio) {
    status = pdev.MapMmio(kMsrMmio, &msr_mmio);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-clk: failed to map MSR regs, status = %d", status);
      return status;
    }
  }

  ddk::PBusProtocolClient pbus(parent);
  if (!pbus.is_valid()) {
    zxlogf(ERROR, "aml-clk: failed to get platform bus protocol");
    return ZX_ERR_INTERNAL;
  }

  auto clock_device = std::make_unique<amlogic_clock::AmlClock>(
      parent, std::move(*hiu_mmio), *std::move(dosbus_mmio), *std::move(msr_mmio), info.did);

  status = clock_device->DdkAdd("clocks");
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-clk: Could not create clock device: %d", status);
    return status;
  }

  clock_device->Register(pbus);

  // devmgr is now in charge of the memory for dev.
  __UNUSED auto ptr = clock_device.release();
  return ZX_OK;
}

zx_status_t AmlClock::ClkTogglePll(uint32_t clk, const bool enable) {
  if (clk >= HIU_PLL_COUNT) {
    return ZX_ERR_INVALID_ARGS;
  }

  aml_pll_dev_t* target = &plldev_[clk];

  if (enable) {
    return s905d2_pll_ena(target);
  } else {
    s905d2_pll_disable(target);
    return ZX_OK;
  }
}

zx_status_t AmlClock::ClkToggle(uint32_t clk, const bool enable) {
  if (clk >= gate_count_) {
    return ZX_ERR_INVALID_ARGS;
  }

  const meson_clk_gate_t* gate = &(gates_[clk]);

  fbl::AutoLock al(&lock_);

  uint32_t mask = gate->mask ? gate->mask : (1 << gate->bit);
  ddk::MmioBuffer* mmio;
  switch (gate->register_set) {
    case kMesonRegisterSetHiu:
      mmio = &hiu_mmio_;
      break;
    case kMesonRegisterSetDos:
      mmio = &dosbus_mmio_;
      break;
    default:
      ZX_ASSERT(false);
  }
  if (enable) {
    mmio->SetBits32(mask, gate->reg);
  } else {
    mmio->ClearBits32(mask, gate->reg);
  }

  return ZX_OK;
}

zx_status_t AmlClock::ClockImplEnable(uint32_t clk) {
  // Determine which clock type we're trying to control.
  aml_clk_common::aml_clk_type type = aml_clk_common::AmlClkType(clk);
  const uint16_t clkid = aml_clk_common::AmlClkIndex(clk);

  switch (type) {
    case aml_clk_common::aml_clk_type::kMesonGate:
      return ClkToggle(clkid, true);
    case aml_clk_common::aml_clk_type::kMesonPll:
      return ClkTogglePll(clkid, true);
    default:
      // Not a supported clock type?
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t AmlClock::ClockImplDisable(uint32_t clk) {
  // Determine which clock type we're trying to control.
  aml_clk_common::aml_clk_type type = aml_clk_common::AmlClkType(clk);
  const uint16_t clkid = aml_clk_common::AmlClkIndex(clk);

  switch (type) {
    case aml_clk_common::aml_clk_type::kMesonGate:
      return ClkToggle(clkid, false);
    case aml_clk_common::aml_clk_type::kMesonPll:
      return ClkTogglePll(clkid, false);
    default:
      // Not a supported clock type?
      return ZX_ERR_NOT_SUPPORTED;
  };
}

zx_status_t AmlClock::ClockImplIsEnabled(uint32_t id, bool* out_enabled) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AmlClock::ClockImplSetRate(uint32_t clk, uint64_t hz) {
  // Determine which clock type we're trying to control.
  aml_clk_common::aml_clk_type type = aml_clk_common::AmlClkType(clk);
  const uint16_t clkid = aml_clk_common::AmlClkIndex(clk);

  if (clkid >= HIU_PLL_COUNT) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (type != aml_clk_common::aml_clk_type::kMesonPll) {
    // For now, only Meson PLLs support rate operation.
    return ZX_ERR_NOT_SUPPORTED;
  }

  aml_pll_dev_t* target = &plldev_[clkid];

  return s905d2_pll_set_rate(target, hz);
}

zx_status_t AmlClock::ClockImplQuerySupportedRate(uint32_t clk, uint64_t max_rate,
                                                  uint64_t* out_best_rate) {
  // Determine which clock type we're trying to control.
  aml_clk_common::aml_clk_type type = aml_clk_common::AmlClkType(clk);
  const uint16_t clkid = aml_clk_common::AmlClkIndex(clk);

  if (clkid >= HIU_PLL_COUNT) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (type != aml_clk_common::aml_clk_type::kMesonPll) {
    // For now, only Meson PLLs support rate operation.
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (out_best_rate == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  const hhi_plls_t pllid = static_cast<hhi_plls_t>(clkid);
  const hhi_pll_rate_t* rate_table = s905d2_pll_get_rate_table(pllid);
  const size_t rate_table_size = s905d2_get_rate_table_count(pllid);
  const hhi_pll_rate_t* best_rate = nullptr;

  for (size_t i = 0; i < rate_table_size; i++) {
    if (rate_table[i].rate <= max_rate) {
      best_rate = &rate_table[i];
    } else {
      break;
    }
  }

  // Couldn't find a rate lower than or equal to max_rate.
  if (!best_rate) {
    return ZX_ERR_NOT_FOUND;
  }

  *out_best_rate = best_rate->rate;

  return ZX_OK;
}

zx_status_t AmlClock::ClockImplGetRate(uint32_t id, uint64_t* out_current_rate) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AmlClock::IsSupportedMux(const uint32_t id, const uint16_t kSupportedMask) {
  const uint16_t index = aml_clk_common::AmlClkIndex(id);
  const uint16_t type = static_cast<uint16_t>(aml_clk_common::AmlClkType(id));

  if ((type & kSupportedMask) == 0) {
    zxlogf(ERROR, "%s: Unsupported mux type for operation, clkid = %u", __func__, id);
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (!muxes_ || mux_count_ == 0) {
    zxlogf(ERROR, "%s: Platform does not have mux support.", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (index >= mux_count_) {
    zxlogf(ERROR, "%s: Mux index out of bounds, count = %lu, idx = %u", __func__, mux_count_,
           index);
    return ZX_ERR_OUT_OF_RANGE;
  }

  return ZX_OK;
}

zx_status_t AmlClock::ClockImplSetInput(uint32_t id, uint32_t idx) {
  constexpr uint16_t kSupported = static_cast<uint16_t>(aml_clk_common::aml_clk_type::kMesonMux);
  zx_status_t st = IsSupportedMux(id, kSupported);
  if (st != ZX_OK) {
    return st;
  }

  const uint16_t index = aml_clk_common::AmlClkIndex(id);

  fbl::AutoLock al(&lock_);

  const meson_clk_mux_t& mux = muxes_[index];

  if (idx >= mux.n_inputs) {
    zxlogf(ERROR, "%s: mux input index out of bounds, max = %u, idx = %u.", __func__,
           mux.n_inputs, idx);
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint32_t clkidx;
  if (mux.inputs) {
    clkidx = mux.inputs[idx];
  } else {
    clkidx = idx;
  }

  uint32_t val = hiu_mmio_.Read32(mux.reg);
  val &= ~(mux.mask << mux.shift);
  val |= (clkidx & mux.mask) << mux.shift;
  hiu_mmio_.Write32(val, mux.reg);

  return ZX_OK;
}

zx_status_t AmlClock::ClockImplGetNumInputs(uint32_t id, uint32_t* out_num_inputs) {
  constexpr uint16_t kSupported =
      (static_cast<uint16_t>(aml_clk_common::aml_clk_type::kMesonMux) |
       static_cast<uint16_t>(aml_clk_common::aml_clk_type::kMesonMuxRo));

  zx_status_t st = IsSupportedMux(id, kSupported);
  if (st != ZX_OK) {
    return st;
  }

  const uint16_t index = aml_clk_common::AmlClkIndex(id);

  const meson_clk_mux_t& mux = muxes_[index];

  *out_num_inputs = mux.n_inputs;

  return ZX_OK;
}

zx_status_t AmlClock::ClockImplGetInput(uint32_t id, uint32_t* out_input) {
  // Bitmask representing clock types that support this operation.
  constexpr uint16_t kSupported =
      (static_cast<uint16_t>(aml_clk_common::aml_clk_type::kMesonMux) |
       static_cast<uint16_t>(aml_clk_common::aml_clk_type::kMesonMuxRo));

  zx_status_t st = IsSupportedMux(id, kSupported);
  if (st != ZX_OK) {
    return st;
  }

  const uint16_t index = aml_clk_common::AmlClkIndex(id);

  const meson_clk_mux_t& mux = muxes_[index];

  const uint32_t result = (hiu_mmio_.Read32(mux.reg) >> mux.shift) & mux.mask;

  if (mux.inputs) {
    for (uint32_t i = 0; i < mux.n_inputs; i++) {
      if (result == mux.inputs[i]) {
        *out_input = i;
        return ZX_OK;
      }
    }
  }

  *out_input = result;
  return ZX_OK;
}

// Note: The clock index taken here are the index of clock
// from the clock table and not the clock_gates index.
// This API measures the clk frequency for clk.
// Following implementation is adopted from Amlogic SDK,
// there is absolutely no documentation.
zx_status_t AmlClock::ClkMeasureUtil(uint32_t clk, uint64_t* clk_freq) {
  if (!msr_mmio_) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Set the measurement gate to 64uS.
  uint32_t value = 64 - 1;
  msr_mmio_->Write32(value, clk_msr_offsets_.reg0_offset);
  // Disable continuous measurement.
  // Disable interrupts.
  value = MSR_CONT | MSR_INTR;
  // Clear the clock source.
  value |= MSR_CLK_SRC_MASK << MSR_CLK_SRC_SHIFT;
  msr_mmio_->ClearBits32(value, clk_msr_offsets_.reg0_offset);

  value = ((clk << MSR_CLK_SRC_SHIFT) |  // Select the MUX.
           MSR_RUN |                     // Enable the clock.
           MSR_ENABLE);                  // Enable measuring.
  msr_mmio_->SetBits32(value, clk_msr_offsets_.reg0_offset);

  // Wait for the measurement to be done.
  for (uint32_t i = 0; i < MSR_WAIT_BUSY_RETRIES; i++) {
    value = msr_mmio_->Read32(clk_msr_offsets_.reg0_offset);
    if (value & MSR_BUSY) {
      // Wait a little bit before trying again.
      zx_nanosleep(zx_deadline_after(ZX_USEC(MSR_WAIT_BUSY_TIMEOUT_US)));
      continue;
    } else {
      // Disable measuring.
      msr_mmio_->ClearBits32(MSR_ENABLE, clk_msr_offsets_.reg0_offset);
      // Get the clock value.
      value = msr_mmio_->Read32(clk_msr_offsets_.reg2_offset);
      // Magic numbers, since lack of documentation.
      *clk_freq = (((value + 31) & MSR_VAL_MASK) / 64);
      return ZX_OK;
    }
  }
  return ZX_ERR_TIMED_OUT;
}

zx_status_t AmlClock::ClkMeasure(uint32_t clk, fuchsia_hardware_clock_FrequencyInfo* info) {
  if (clk >= clk_table_count_) {
    return ZX_ERR_INVALID_ARGS;
  }

  size_t max_len = sizeof(info->name);
  size_t len = strnlen(clk_table_[clk], max_len);
  if (len == max_len) {
    return ZX_ERR_INVALID_ARGS;
  }

  memcpy(info->name, clk_table_[clk], len + 1);
  return ClkMeasureUtil(clk, &info->frequency);
}

uint32_t AmlClock::GetClkCount() { return static_cast<uint32_t>(clk_table_count_); }

void AmlClock::ShutDown() {
  hiu_mmio_.reset();

  if (msr_mmio_) {
    msr_mmio_->reset();
  }
}

void AmlClock::Register(const ddk::PBusProtocolClient& pbus) {
  clock_impl_protocol_t clk_proto = {
      .ops = &clock_impl_protocol_ops_,
      .ctx = this,
  };

  pbus.RegisterProtocol(ZX_PROTOCOL_CLOCK_IMPL, &clk_proto, sizeof(clk_proto));
}

zx_status_t fidl_clk_measure(void* ctx, uint32_t clk, fidl_txn_t* txn) {
  auto dev = static_cast<AmlClock*>(ctx);
  fuchsia_hardware_clock_FrequencyInfo info;

  dev->ClkMeasure(clk, &info);

  return fuchsia_hardware_clock_DeviceMeasure_reply(txn, &info);
}

zx_status_t fidl_clk_get_count(void* ctx, fidl_txn_t* txn) {
  auto dev = static_cast<AmlClock*>(ctx);

  return fuchsia_hardware_clock_DeviceGetCount_reply(txn, dev->GetClkCount());
}

static const fuchsia_hardware_clock_Device_ops_t fidl_ops_ = {
    .Measure = fidl_clk_measure,
    .GetCount = fidl_clk_get_count,
};

zx_status_t AmlClock::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_clock_Device_dispatch(this, txn, msg, &fidl_ops_);
}

void AmlClock::InitHiu() {
  // TODO(fxb/56253): Add MMIO_PTR to cast.
  s905d2_hiu_init_etc(&hiudev_, static_cast<uint8_t*>((void*)hiu_mmio_.get()));
  for (unsigned int pllnum = 0; pllnum < HIU_PLL_COUNT; pllnum++) {
    const hhi_plls_t pll = static_cast<hhi_plls_t>(pllnum);
    s905d2_pll_init_etc(&hiudev_, &plldev_[pllnum], pll);
  }
}

void AmlClock::DdkUnbindNew(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

void AmlClock::DdkRelease() { delete this; }

}  // namespace amlogic_clock

zx_status_t aml_clk_bind(void* ctx, zx_device_t* parent) {
  return amlogic_clock::AmlClock::Create(parent);
}

static constexpr zx_driver_ops_t aml_clk_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = aml_clk_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_clk, aml_clk_driver_ops, "zircon", "0.1", 7)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    // we support multiple SOC variants.
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_AXG_CLK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_GXL_CLK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_G12A_CLK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_G12B_CLK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_SM1_CLK),
ZIRCON_DRIVER_END(aml_clk)
