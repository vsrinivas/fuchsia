// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>

#include <algorithm>
#include <iterator>

#include <soc/aml-a5/a5-hiu-regs.h>
#include <soc/aml-a5/a5-hiu.h>

namespace amlogic_clock::a5 {

static constexpr uint32_t kPllStableTimeUs = 50;

void AmlA5HifiPllDevice::InitPll() {
  auto pll_ctrl = HifiPllCtrl::Get().ReadFrom(&view_);

  pll_ctrl.set_reset(1).WriteTo(&view_);
  LoadInitConfig(view_, *data_);
  pll_ctrl.set_reset(0).WriteTo(&view_);
}

void AmlA5HifiPllDevice::Disable() {
  HifiPllCtrl::Get()
      .ReadFrom(&view_)
      .set_reset(1)   // Put the pll is in reset
      .set_enable(0)  // Disable the pll
      .WriteTo(&view_);
}

zx_status_t AmlA5HifiPllDevice::Enable() {
  auto pll_ctrl = HifiPllCtrl::Get().ReadFrom(&view_);

  if (pll_ctrl.reset() || !pll_ctrl.enable() || !pll_ctrl.lock()) {
    pll_ctrl
        .set_reset(1)   // Make sure the pll is in reset
        .set_enable(1)  // Enable the pll
        .WriteTo(&view_);

    // Add some delays to stabilize PLL.
    // If not, it may lock failed.
    zx_nanosleep(zx_deadline_after(ZX_USEC(kPllStableTimeUs)));

    // Take the pll out reset
    pll_ctrl.set_reset(0).WriteTo(&view_);

    uint32_t lock_retry = 1000;
    do {
      if (pll_ctrl.ReadFrom(&view_).lock()) {
        return ZX_OK;
      }
      zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
    } while (lock_retry--);

    return ZX_ERR_TIMED_OUT;
  }

  return ZX_OK;
}

zx_status_t AmlA5HifiPllDevice::SetRate(const uint64_t hz) {
  const hhi_pll_rate_t* pll_rate;
  zx_status_t status = FetchRateTable(hz, rates_table_, &pll_rate);
  if (status != ZX_OK) {
    return status;
  }

  const uint32_t n = pll_rate->n;
  const uint32_t m = pll_rate->m;
  const uint32_t od = pll_rate->od;
  const uint32_t frac = pll_rate->frac;
  // Note:
  //  frac_max = 1 << (frac_reg_width - 2)
  //  out = [ 24M * (m + frac / frac_max) / n ] / ( 1 << od)
  //
  auto pll_ctrl = HifiPllCtrl::Get().ReadFrom(&view_);

  if (pll_ctrl.enable()) {
    Disable();
  }

  pll_ctrl.set_n(n)
      .set_m(m)
      .set_od(od)  // Set Output divider
      .WriteTo(&view_);

  auto pll_ctrl2 = HifiPllCtrl2::Get().ReadFrom(&view_);
  pll_ctrl2.set_frac(frac).WriteTo(&view_);

  return Enable();
}

void AmlA5MpllDevice::InitPll() { LoadInitConfig(view_, *data_); }

void AmlA5MpllDevice::Disable() { MpllCtrl::Get().ReadFrom(&view_).set_enable(0).WriteTo(&view_); }

zx_status_t AmlA5MpllDevice::Enable() {
  auto mpll_ctrl = MpllCtrl::Get().ReadFrom(&view_);

  if (mpll_ctrl.enable()) {
    return ZX_OK;
  }

  // Enable Clock
  mpll_ctrl.set_enable(1).WriteTo(&view_);

  return ZX_OK;
}

zx_status_t AmlA5MpllDevice::SetRate(const uint64_t hz) {
  const hhi_pll_rate_t* pll_rate;
  zx_status_t status = FetchRateTable(hz, rates_table_, &pll_rate);
  if (status != ZX_OK) {
    return status;
  }

  const uint32_t sdm_in = pll_rate->frac;
  const uint32_t n_in = pll_rate->n;
  // mpll rate = 2.0G / (n_in + sdm_in / 16384)
  //
  // e.g.
  //  set mpll to 491'520'000 hz.
  //  1. get fractional part
  //    frac = (2G % 491'520'000) * 16384 = 555'745'280'000
  //    sdm_in = frac / 491'520'000
  //           = 1130.6
  //           = 1131 (div round up)
  //  2. get integer divider part
  //    n_in = 2G / 491'520'000
  //         = 4
  //
  MpllCtrl::Get()
      .ReadFrom(&view_)
      .set_sdm_in(sdm_in)  // Set the fractional part
      .set_n_in(n_in)      // Set the integer divider part
      .WriteTo(&view_);

  return ZX_OK;
}

}  // namespace amlogic_clock::a5
