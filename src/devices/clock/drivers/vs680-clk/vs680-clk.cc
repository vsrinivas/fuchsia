// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vs680-clk.h"

#include <lib/device-protocol/pdev.h>
#include <stdint.h>

#include <ddk/binding.h>
#include <ddk/platform-defs.h>
#include <fbl/auto_lock.h>
#include <soc/vs680/vs680-clk.h>

#include "vs680-clk-reg.h"

namespace {

constexpr uint32_t kSourceClockFreqHz = 25'000'000;
constexpr uint32_t kPllMinFreqHz = 20'000'000;

constexpr uint32_t kFractionBits = 24;
// The VCO always has an additional division by 4.
constexpr uint32_t kFixedFeedbackShift = 2;
// Only shift left 22 in calculations, instead of shifting left 24 then right 2.
constexpr uint32_t kOutputShift = kFractionBits - kFixedFeedbackShift;

constexpr uint32_t kOutputDividerShift = 1;

constexpr uint32_t kPllLockTimeMicroseconds = 120;

struct {
  vs680::ClockMmio pll_mmio;
  uint16_t pll_offset;   // The offset of the PLL registers in pll_mmio.
  uint32_t max_freq_hz;  // The maximum frequency for this PLL. Must be <= 5.12 GHz.

  vs680::ClockMmio bypass_mmio;
  uint8_t bypass_bit;      // The bit that controls bypass for this PLL.
  uint16_t bypass_offset;  // The offset of the bypass register in bypass_mmio
} constexpr kPllConfigs[] = {
    [vs680::kSysPll0] =
        {
            .pll_mmio = vs680::kChipCtrlMmio,
            .pll_offset = 0x200,
            .max_freq_hz = 1'200'000'000,
            .bypass_mmio = vs680::kChipCtrlMmio,
            .bypass_bit = 0,
            .bypass_offset = 0x710,
        },
    [vs680::kSysPll1] =
        {
            .pll_mmio = vs680::kChipCtrlMmio,
            .pll_offset = 0x220,
            .max_freq_hz = 1'200'000'000,
            .bypass_mmio = vs680::kChipCtrlMmio,
            .bypass_bit = 1,
            .bypass_offset = 0x710,
        },
    [vs680::kSysPll2] =
        {
            .pll_mmio = vs680::kChipCtrlMmio,
            .pll_offset = 0x240,
            .max_freq_hz = 1'200'000'000,
            .bypass_mmio = vs680::kChipCtrlMmio,
            .bypass_bit = 2,
            .bypass_offset = 0x710,
        },
    [vs680::kCpuPll] =
        {
            .pll_mmio = vs680::kCpuPllMmio,
            .pll_offset = 0x0,
            .max_freq_hz = 2'200'000'000,
            .bypass_mmio = vs680::kChipCtrlMmio,
            .bypass_bit = 4,
            .bypass_offset = 0x710,
        },
    [vs680::kVPll0] =
        {
            .pll_mmio = vs680::kAvioMmio,
            .pll_offset = 0x4,
            .max_freq_hz = 1'200'000'000,
            .bypass_mmio = vs680::kAvioMmio,
            .bypass_bit = 0,
            .bypass_offset = 0x130,
        },
    [vs680::kVPll1] =
        {
            .pll_mmio = vs680::kAvioMmio,
            .pll_offset = 0x70,
            .max_freq_hz = 1'200'000'000,
            .bypass_mmio = vs680::kAvioMmio,
            .bypass_bit = 1,
            .bypass_offset = 0x130,
        },
    [vs680::kAPll0] =
        {
            .pll_mmio = vs680::kAvioMmio,
            .pll_offset = 0x28,
            .max_freq_hz = 1'200'000'000,
            .bypass_mmio = vs680::kAvioMmio,
            .bypass_bit = 2,
            .bypass_offset = 0x130,
        },
    [vs680::kAPll1] =
        {
            .pll_mmio = vs680::kAvioMmio,
            .pll_offset = 0x4c,
            .max_freq_hz = 1'200'000'000,
            .bypass_mmio = vs680::kAvioMmio,
            .bypass_bit = 3,
            .bypass_offset = 0x130,
        },
};

struct PllParameters {
  uint32_t reference_divider;
  uint64_t feedback_divider;
  uint32_t output_divider;
  uint32_t range;
};

PllParameters GetPllParameters(const uint64_t hz) {
  constexpr uint64_t kMaxDivFI = 0x200;

  // Using a reference divider of 5 sets the PLL input frequency to 5 MHz, which can work with both
  // integer and fractional modes.
  constexpr uint32_t kReferenceDivider = 5;
  constexpr uint32_t kRange = 0b000;  // This corresponds to a PLL input frequency of 5 - 7.5 MHz.
  constexpr uint32_t kOutputDivider = 2;  // The minimum output divider, chosen for convenience.

  // Make sure we can shift left without losing bits. This is just a sanity check as the frequency
  // that would overflow a uint64 is much greater than any PLL max frequency.
  ZX_DEBUG_ASSERT((hz * kReferenceDivider * kOutputDivider) < (1ULL << (64 - kOutputShift)));

  const uint64_t feedback_divider =
      ((hz * kReferenceDivider * kOutputDivider) << kOutputShift) / kSourceClockFreqHz;

  // Another sanity check, as the max feedback divider corresponds to a PLL output frequency of over
  // 5.12 GHz.
  ZX_DEBUG_ASSERT((feedback_divider >> kFractionBits) <= kMaxDivFI);

  return {
      .reference_divider = kReferenceDivider,
      .feedback_divider = feedback_divider,
      .output_divider = kOutputDivider,
      .range = kRange,
  };
}

}  // namespace

namespace clk {

zx_status_t Vs680Clk::Create(void* ctx, zx_device_t* parent) {
  ddk::PDev pdev(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: failed to get pdev\n", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  std::optional<ddk::MmioBuffer> avio_mmio, cpu_pll_mmio, chip_ctrl_mmio;

  zx_status_t status = pdev.MapMmio(vs680::kAvioMmio, &avio_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to map AVIO MMIO: %d\n", __FILE__, status);
    return status;
  }
  if ((status = pdev.MapMmio(vs680::kCpuPllMmio, &cpu_pll_mmio)) != ZX_OK) {
    zxlogf(ERROR, "%s: failed to map CPUPLL MMIO: %d\n", __FILE__, status);
    return status;
  }
  if ((status = pdev.MapMmio(vs680::kChipCtrlMmio, &chip_ctrl_mmio)) != ZX_OK) {
    zxlogf(ERROR, "%s: failed to map chip ctrl MMIO: %d\n", __FILE__, status);
    return status;
  }

  std::unique_ptr<Vs680Clk> device(new Vs680Clk(
      parent, *std::move(avio_mmio), *std::move(cpu_pll_mmio), *std::move(chip_ctrl_mmio)));

  status = device->DdkAdd("vs680-clk");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed %d\n", __FILE__, status);
    return status;
  }

  __UNUSED auto _ = device.release();
  return ZX_OK;
}

zx_status_t Vs680Clk::ClockImplEnable(uint32_t id) {
  if (id >= fbl::count_of(kPllConfigs)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // TODO(bradenkell): Add support for enabling A/VPLLs.
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vs680Clk::ClockImplDisable(uint32_t id) {
  if (id >= fbl::count_of(kPllConfigs)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // TODO(bradenkell): Add support for disabling A/VPLLs.
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vs680Clk::ClockImplIsEnabled(uint32_t id, bool* out_enabled) {
  if (id >= fbl::count_of(kPllConfigs)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // TODO(bradenkell): Add support for disabling A/VPLLs.
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vs680Clk::ClockImplSetRate(uint32_t id, uint64_t hz) {
  if (id >= fbl::count_of(kPllConfigs)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const auto& config = kPllConfigs[id];
  if (hz > config.max_freq_hz || hz < kPllMinFreqHz) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  fbl::AutoLock lock(&lock_);
  ZX_DEBUG_ASSERT(config.pll_mmio < fbl::count_of(mmios_));
  ZX_DEBUG_ASSERT(config.bypass_mmio < fbl::count_of(mmios_));

  mmios_[config.bypass_mmio].SetBit<uint32_t>(config.bypass_bit, config.bypass_offset);

  const ddk::MmioBuffer& pll_mmio = mmios_[config.pll_mmio];

  PllCtrlA::Get(config.pll_offset)
      .ReadFrom(&pll_mmio)
      .set_bypass(1)
      .WriteTo(&pll_mmio)
      .set_reset(1)
      .WriteTo(&pll_mmio);

  const PllParameters params = GetPllParameters(hz);

  PllCtrlC::Get(config.pll_offset)
      .ReadFrom(&pll_mmio)
      .set_divr(params.reference_divider - 1)
      .WriteTo(&pll_mmio);

  PllCtrlD::Get(config.pll_offset)
      .ReadFrom(&pll_mmio)
      .set_divfi(static_cast<uint32_t>((params.feedback_divider >> kFractionBits) - 1))
      .WriteTo(&pll_mmio);

  PllCtrlE::Get(config.pll_offset)
      .ReadFrom(&pll_mmio)
      .set_divff(params.feedback_divider & ((1 << kFractionBits) - 1))
      .WriteTo(&pll_mmio);

  PllCtrlF::Get(config.pll_offset)
      .ReadFrom(&pll_mmio)
      .set_divq((params.output_divider >> kOutputDividerShift) - 1)
      .WriteTo(&pll_mmio);

  PllCtrlA::Get(config.pll_offset).ReadFrom(&pll_mmio).set_range(params.range).WriteTo(&pll_mmio);

  zx::nanosleep(zx::deadline_after(zx::sec(pll_reset_time_seconds_)));

  PllCtrlA::Get(config.pll_offset).ReadFrom(&pll_mmio).set_reset(0).WriteTo(&pll_mmio);

  PllStatus status = PllStatus::Get(config.pll_offset).FromValue(0);
  for (uint32_t i = 0; i < kPllLockTimeMicroseconds; i++) {
    if (status.ReadFrom(&pll_mmio).lock()) {
      break;
    }
    zx::nanosleep(zx::deadline_after(zx::usec(1)));
  }

  if (!status.lock()) {
    zxlogf(TRACE, "%s: PLL %u did not lock in %u us\n", __FILE__, id, kPllLockTimeMicroseconds);
  }

  PllCtrlA::Get(config.pll_offset).ReadFrom(&pll_mmio).set_bypass(0).WriteTo(&pll_mmio);

  mmios_[config.bypass_mmio].ClearBit<uint32_t>(config.bypass_bit, config.bypass_offset);

  return ZX_OK;
}

zx_status_t Vs680Clk::ClockImplQuerySupportedRate(uint32_t id, uint64_t hz, uint64_t* out_hz) {
  if (id >= fbl::count_of(kPllConfigs)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (hz < kPllMinFreqHz) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (hz > kPllConfigs[id].max_freq_hz) {
    return kPllConfigs[id].max_freq_hz;
  }

  const PllParameters params = GetPllParameters(hz);
  *out_hz = ((kSourceClockFreqHz * params.feedback_divider) /
             (params.reference_divider * params.output_divider)) >>
            kOutputShift;
  return ZX_OK;
}

zx_status_t Vs680Clk::ClockImplGetRate(uint32_t id, uint64_t* out_hz) {
  if (id >= fbl::count_of(kPllConfigs)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const auto& config = kPllConfigs[id];

  fbl::AutoLock lock(&lock_);
  ZX_DEBUG_ASSERT(config.pll_mmio < fbl::count_of(mmios_));
  ZX_DEBUG_ASSERT(config.bypass_mmio < fbl::count_of(mmios_));

  if (mmios_[config.bypass_mmio].GetBit<uint32_t>(config.bypass_bit, config.bypass_offset)) {
    *out_hz = kSourceClockFreqHz;
    return ZX_OK;
  }

  const ddk::MmioBuffer& pll_mmio = mmios_[config.pll_mmio];
  if (PllCtrlA::Get(config.pll_offset).ReadFrom(&pll_mmio).bypass()) {
    *out_hz = kSourceClockFreqHz;
    return ZX_OK;
  }

  // All values need to be incremented to get the effective amount of division, except for the
  // feedback fractional component (DVIFF). Additionally, DVIQ needs to be shifted left one to get
  // the post divider.
  const uint64_t divr = PllCtrlC::Get(config.pll_offset).ReadFrom(&pll_mmio).divr() + 1;
  const uint64_t divfi = PllCtrlD::Get(config.pll_offset).ReadFrom(&pll_mmio).divfi() + 1;
  const uint64_t divff = PllCtrlE::Get(config.pll_offset).ReadFrom(&pll_mmio).divff();
  const uint64_t divq = PllCtrlF::Get(config.pll_offset).ReadFrom(&pll_mmio).divq() + 1;

  const uint64_t feedback_divider = divff | (divfi << kFractionBits);
  const uint64_t output_divider = divq << kOutputDividerShift;

  *out_hz = ((kSourceClockFreqHz * feedback_divider) / (divr * output_divider)) >> kOutputShift;
  return ZX_OK;
}

zx_status_t Vs680Clk::ClockImplSetInput(uint32_t id, uint32_t idx) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Vs680Clk::ClockImplGetNumInputs(uint32_t id, uint32_t* out_n) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vs680Clk::ClockImplGetInput(uint32_t id, uint32_t* out_index) {
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace clk

static constexpr zx_driver_ops_t vs680_clk_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = clk::Vs680Clk::Create;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(vs680_clk, vs680_clk_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_SYNAPTICS),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_VS680_CLOCK),
ZIRCON_DRIVER_END(vs680_clk)
// clang-format on
