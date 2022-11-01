// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <algorithm>
#include <iterator>

#include <soc/aml-a1/a1-hiu-regs.h>
#include <soc/aml-a1/a1-hiu.h>

namespace amlogic_clock::a1 {

zx_status_t PllClkWaitLock(const fdf::MmioView view) {
  constexpr uint32_t kPllStableTimeUs = 10;
  uint32_t retry = 5;

  do {
    zx_nanosleep(zx_deadline_after(ZX_USEC(kPllStableTimeUs)));
    if (PllSts::Get().ReadFrom(&view).lock()) {
      return ZX_OK;
    }

  } while (retry--);

  return ZX_ERR_TIMED_OUT;
}

void UpdateSettings(const fdf::MmioView view, const meson_clk_pll_data_t config,
                    const hhi_pll_rate_t pll_rate) {
  for (uint32_t i = 0; i < config.init_count; i++) {
    if (config.init_regs[i].reg_offset == PllCtrl0::offset) {
      PllCtrl0::Get()  // Update M, N
          .FromValue(config.init_regs[i].def)
          .set_m(pll_rate.m)
          .set_n(pll_rate.n)
          .WriteTo(&view);
    } else if (config.init_regs[i].reg_offset == PllCtrl1::offset) {
      PllCtrl1::Get()  // Update FRAC
          .FromValue(config.init_regs[i].def)
          .set_frac(pll_rate.frac)
          .WriteTo(&view);
    } else {
      view.Write32(config.init_regs[i].def, config.init_regs[i].reg_offset);
    }

    if (config.init_regs[i].delay_us) {
      zx_nanosleep(zx_deadline_after(ZX_USEC(config.init_regs[i].delay_us)));
    }
  }
}

void AmlA1PllDevice::Disable() {
  PllCtrl0::Get()
      .ReadFrom(&view_)
      .set_enable(0)  // Disable the pll
      .WriteTo(&view_);
}

zx_status_t AmlA1PllDevice::Enable() {
  zx_status_t status;
  auto ctrl0 = PllCtrl0::Get().ReadFrom(&view_);

  if (ctrl0.enable()) {
    return ZX_OK;
  }

  status = SetRate(current_rate_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "pll enable failed.");
  }

  return status;
}

zx_status_t AmlA1PllDevice::SetRate(const uint64_t hz) {
  const hhi_pll_rate_t* pll_rate;
  const bool repeatedly_toggling = data_->repeatedly_toggling;

  zx_status_t status = FetchRateTable(hz, rates_table_, &pll_rate);
  if (status != ZX_OK) {
    return status;
  }

  if (!repeatedly_toggling) {
    auto ctrl0 = PllCtrl0::Get().ReadFrom(&view_);
    if (ctrl0.enable()) {
      Disable();
    }
  }

  uint32_t retry = 10;
  do {
    if (repeatedly_toggling) {
      auto ctrl0 = PllCtrl0::Get().ReadFrom(&view_);
      if (ctrl0.enable()) {
        Disable();
      }
    }

    UpdateSettings(view_, *data_, *pll_rate);

    if (PllClkWaitLock(view_) == ZX_OK) {
      break;
    }

  } while (retry--);

  if (retry == 0) {
    zxlogf(ERROR, "pll locked failed.");
    return ZX_ERR_TIMED_OUT;
  }

  return ZX_OK;
}

}  // namespace amlogic_clock::a1
