// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vs680-clk-types.h"

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

zx_status_t Vs680Pll::SetRate(uint64_t hz) const {
  if (hz > max_freq_hz() || hz < kPllMinFreqHz) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  StartPllChange();

  PllCtrlA::Get()
      .ReadFrom(&pll_mmio_)
      .set_bypass(1)
      .WriteTo(&pll_mmio_)
      .set_reset(1)
      .WriteTo(&pll_mmio_);

  const PllParameters params = GetPllParameters(hz);

  PllCtrlC::Get().ReadFrom(&pll_mmio_).set_divr(params.reference_divider - 1).WriteTo(&pll_mmio_);

  PllCtrlD::Get()
      .ReadFrom(&pll_mmio_)
      .set_divfi(static_cast<uint32_t>((params.feedback_divider >> kFractionBits) - 1))
      .WriteTo(&pll_mmio_);

  PllCtrlE::Get()
      .ReadFrom(&pll_mmio_)
      .set_divff(params.feedback_divider & ((1 << kFractionBits) - 1))
      .WriteTo(&pll_mmio_);

  PllCtrlF::Get()
      .ReadFrom(&pll_mmio_)
      .set_divq((params.output_divider >> kOutputDividerShift) - 1)
      .WriteTo(&pll_mmio_);

  PllCtrlA::Get().ReadFrom(&pll_mmio_).set_range(params.range).WriteTo(&pll_mmio_);

  zx::nanosleep(zx::deadline_after(reset_time_));

  PllCtrlA::Get().ReadFrom(&pll_mmio_).set_reset(0).WriteTo(&pll_mmio_);

  PllStatus status = PllStatus::Get().FromValue(0);
  for (uint32_t i = 0; i < kPllLockTimeMicroseconds; i++) {
    if (status.ReadFrom(&pll_mmio_).lock()) {
      break;
    }
    zx::nanosleep(zx::deadline_after(zx::usec(1)));
  }

  if (!status.lock()) {
    zxlogf(TRACE, "%s: PLL did not lock in %u us\n", __FILE__, kPllLockTimeMicroseconds);
  }

  PllCtrlA::Get().ReadFrom(&pll_mmio_).set_bypass(0).WriteTo(&pll_mmio_);

  EndPllChange();

  return ZX_OK;
}

zx_status_t Vs680Pll::QuerySupportedRate(uint64_t hz, uint64_t* out_hz) const {
  if (hz < kPllMinFreqHz) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (hz > max_freq_hz()) {
    hz = max_freq_hz();
  }

  const PllParameters params = GetPllParameters(hz);
  *out_hz = ((kSourceClockFreqHz * params.feedback_divider) /
             (params.reference_divider * params.output_divider)) >>
            kOutputShift;
  return ZX_OK;
}

zx_status_t Vs680Pll::GetRate(uint64_t* out_hz) const {
  if (PllCtrlA::Get().ReadFrom(&pll_mmio_).bypass()) {
    *out_hz = kSourceClockFreqHz;
    return ZX_OK;
  }

  // All values need to be incremented to get the effective amount of division, except for the
  // feedback fractional component (DVIFF). Additionally, DVIQ needs to be shifted left one to get
  // the post divider.
  const uint64_t divr = PllCtrlC::Get().ReadFrom(&pll_mmio_).divr() + 1;
  const uint64_t divfi = PllCtrlD::Get().ReadFrom(&pll_mmio_).divfi() + 1;
  const uint64_t divff = PllCtrlE::Get().ReadFrom(&pll_mmio_).divff();
  const uint64_t divq = PllCtrlF::Get().ReadFrom(&pll_mmio_).divq() + 1;

  const uint64_t feedback_divider = divff | (divfi << kFractionBits);
  const uint64_t output_divider = divq << kOutputDividerShift;

  *out_hz = ((kSourceClockFreqHz * feedback_divider) / (divr * output_divider)) >> kOutputShift;
  return ZX_OK;
}

zx_status_t Vs680Pll::SetInput(uint32_t idx) const { return ZX_ERR_NOT_SUPPORTED; }
zx_status_t Vs680Pll::GetNumInputs(uint32_t* out_n) const { return ZX_ERR_NOT_SUPPORTED; }
zx_status_t Vs680Pll::GetInput(uint32_t* out_index) const { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Vs680SysPll::Enable() const { return ZX_ERR_NOT_SUPPORTED; }
zx_status_t Vs680SysPll::Disable() const { return ZX_ERR_NOT_SUPPORTED; }
zx_status_t Vs680SysPll::IsEnabled(bool* out_enabled) const { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Vs680SysPll::GetRate(uint64_t* out_hz) const {
  if (bypass_mmio_.GetBit<uint32_t>(bypass_bit_, 0)) {
    *out_hz = kSourceClockFreqHz;
    return ZX_OK;
  }

  return Vs680Pll::GetRate(out_hz);
}

void Vs680SysPll::StartPllChange() const { bypass_mmio_.SetBit<uint32_t>(bypass_bit_, 0); }
void Vs680SysPll::EndPllChange() const { bypass_mmio_.ClearBit<uint32_t>(bypass_bit_, 0); }

zx_status_t Vs680AVPll::Enable() const {
  disable_mmio_.ClearBit<uint32_t>(disable_bit_, 0);
  return ZX_OK;
}

zx_status_t Vs680AVPll::Disable() const {
  disable_mmio_.SetBit<uint32_t>(disable_bit_, 0);
  return ZX_OK;
}

zx_status_t Vs680AVPll::IsEnabled(bool* out_enabled) const {
  *out_enabled = !disable_mmio_.GetBit<uint32_t>(disable_bit_, 0);
  return ZX_OK;
}

void Vs680AVPll::StartPllChange() const { Disable(); }
void Vs680AVPll::EndPllChange() const { Enable(); }

}  // namespace clk
