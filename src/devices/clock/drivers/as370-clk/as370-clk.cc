// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "as370-clk.h"

#include <lib/device-protocol/pdev.h>

#include <numeric>

#include <ddk/binding.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/clockimpl.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/protocol/platform/bus.h>
#include <fbl/auto_lock.h>
#include <hwreg/bitfields.h>
#include <soc/as370/as370-audio-regs.h>
#include <soc/as370/as370-clk-regs.h>
#include <soc/as370/as370-clk.h>

namespace clk {

zx_status_t As370Clk::Create(void* ctx, zx_device_t* parent) {
  ddk::PDev pdev(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: failed to get pdev", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  std::optional<ddk::MmioBuffer> global_mmio, avio_mmio, cpu_mmio;
  auto status = pdev.MapMmio(0, &global_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to map mmio index 0 %d", __FILE__, status);
    return status;
  }
  status = pdev.MapMmio(1, &avio_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to map mmio index 1 %d", __FILE__, status);
    return status;
  }
  status = pdev.MapMmio(2, &cpu_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to map mmio index 2 %d", __FILE__, status);
    return status;
  }

  std::unique_ptr<As370Clk> device(
      new As370Clk(parent, *std::move(global_mmio), *std::move(avio_mmio), *std::move(cpu_mmio)));

  status = device->DdkAdd("synaptics-clk");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed %d", __FILE__, status);
    return status;
  }

  // Intentially leak, devmgr owns the memory now.
  __UNUSED auto* unused = device.release();

  return ZX_OK;
}

zx_status_t As370Clk::AvpllClkEnable(bool avpll0, bool enable) {
  uint32_t id = avpll0 ? 0 : 1;
  fbl::AutoLock lock(&lock_);
  // TODO(andresoportus): Manage dependencies between AVPLLs, avioSysClk and SYSPLL.
  // For now make sure things get enabled.
  if (enable) {
    // Enable/disable AVIO clk and keep SYSPLL DIV3 as source.
    avioSysClk_ctrl::Get().ReadFrom(&global_mmio_).set_ClkEn(enable).WriteTo(&global_mmio_);

    // Enable sysPll by disabling power down (or vice versa).
    sysPll_ctrl::Get().ReadFrom(&global_mmio_).set_PD(!enable).WriteTo(&global_mmio_);
  }

  if (avpll0) {
    // Enable/disable AVPLL0.
    avioGbl_AVPLLA_CLK_EN::Get().ReadFrom(&avio_mmio_).set_ctrl_AVPLL0(enable).WriteTo(&avio_mmio_);
  } else {
    // Enable/disable AVPLL1.
    avioGbl_AVPLLA_CLK_EN::Get().ReadFrom(&avio_mmio_).set_ctrl_AVPLL1(enable).WriteTo(&avio_mmio_);
  }
  // Enable/disable AVPLLx clock.
  avioGbl_AVPLLx_WRAP_AVPLL_CLK1_CTRL::Get(id)
      .ReadFrom(&avio_mmio_)
      .set_clkEn(enable)
      .WriteTo(&avio_mmio_);

  return ZX_OK;
}

zx_status_t As370Clk::CpuSetRate(uint64_t rate) {
  if (rate < 100'000'000 || rate > 1'800'000'000) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (rate > 400'000'000) {
    auto dn = static_cast<uint32_t>(rate) / 1'000 * 72 / 1'800'000;
    CPU_WRP_PLL_REG_ctrl::Get()
        .FromValue(0)
        .set_FRAC_READY(1)
        .set_MODE(0)
        .set_DN(dn)
        .set_DM(1)
        .set_RESETN(1)
        .WriteTo(&cpu_mmio_);
    CPU_WRP_PLL_REG_ctrl1::Get().FromValue(0).set_FRAC(0).WriteTo(&cpu_mmio_);
    CPU_WRP_PLL_REG_ctrl3::Get()
        .FromValue(0)
        .set_DP1(1)
        .set_PDDP1(0)
        .set_DP(1)
        .set_PDDP(0)
        .set_SLOPE(0)
        .WriteTo(&cpu_mmio_);
    zxlogf(DEBUG, "%s %luHz dn %u  dm %u  dp %u", __FILE__, rate, dn, 1, 1);
  } else {
    auto dn = static_cast<uint32_t>(rate) / 1'000 * 48 / 400'000;
    CPU_WRP_PLL_REG_ctrl::Get()
        .FromValue(0)
        .set_FRAC_READY(1)
        .set_MODE(0)
        .set_DN(dn)
        .set_DM(1)
        .set_RESETN(1)
        .WriteTo(&cpu_mmio_);
    CPU_WRP_PLL_REG_ctrl1::Get().FromValue(0).set_FRAC(0).WriteTo(&cpu_mmio_);
    CPU_WRP_PLL_REG_ctrl3::Get()
        .FromValue(0)
        .set_DP1(1)
        .set_PDDP1(0)
        .set_DP(3)
        .set_PDDP(0)
        .set_SLOPE(0)
        .WriteTo(&cpu_mmio_);
    zxlogf(DEBUG, "%s %luHz dn %u  dm %u  dp %u", __FILE__, rate, dn, 1, 3);
  }
  return ZX_OK;
}

zx_status_t As370Clk::AvpllSetRate(bool avpll0, uint64_t rate) {
  // rate = (frac / (max_frac+1) + dn) * ref_clk / dm / dp.
  // frac = (rate * dp * dm / ref_clk - dn) * (max_frac+1).

  // For 48KHz we need APLL = 196.608MHz.
  // 196.608MHz / 8 = 24.576MHz (MCLK) / 8 = 3.072MHz (BCLK) / 64 = 48KHz (FSYNC).
  // APLL rate = [frac (842887) / 16777216 + dn (55)] * ref_clk (25MHz) / dp (7) = 196.608MHz.

  // For 44.1KHz we need APLL = 180.633600MHz.
  // 180.633600MHz / 8 = 22.579200MHz (MCLK) / 8 = 2.822400MHz (BCLK) / 64 = 44.1KHz (FSYNC).
  // APLL rate = [frac (9687298) / 16777216 + dn (50)] * ref_clk (25MHz) / dp (7) = 180.6336MHz.

  uint32_t id = avpll0 ? 0 : 1;

  constexpr uint64_t max_rate = 800'000'000;  // HW envelope limit.
  if (rate > max_rate) {
    return ZX_ERR_INVALID_ARGS;
  }

  // TODO(andresoportus): Make relative to parent once available in clock framework.
  constexpr uint64_t parent_rate = 25'000'000;  // Main oscilator at 25MHz.
  constexpr uint32_t max_dn = 0x7ff;
  constexpr uint32_t max_frac = 0xffffff;
  constexpr uint32_t dp = 7;
  constexpr uint32_t dm = 1;
  uint32_t dn = static_cast<uint32_t>(rate * dm * dp / parent_rate);
  if (dn > max_dn) {
    return ZX_ERR_INTERNAL;  // Should not happen.
  }

  // It is ok for this calculation to be slow, only done once per PLL.
  uint32_t frac = static_cast<uint32_t>(((static_cast<double>(rate) * dp * dm / parent_rate) - dn) *
                                        (max_frac + 1));
  if (frac > max_frac) {
    return ZX_ERR_INTERNAL;  // Should not happen.
  }

  zxlogf(DEBUG, "%s frac %u  dn %u  dm %u  dp %u", __FILE__, frac, dn, dm, dp);
  zxlogf(DEBUG, "%s requested: %fMHz  expected: %fMHz", __FILE__,
         static_cast<double>(rate) / 1'000'000.,
         ((static_cast<double>(frac) / (max_frac + 1)) + dn) * 25. / dp / dm);

  fbl::AutoLock lock(&lock_);

  if (avpll0) {
    avioGbl_AVPLLA_CLK_EN::Get().ReadFrom(&avio_mmio_).set_ctrl_AVPLL0(0).WriteTo(&avio_mmio_);
  } else {
    avioGbl_AVPLLA_CLK_EN::Get().ReadFrom(&avio_mmio_).set_ctrl_AVPLL1(0).WriteTo(&avio_mmio_);
  }

  avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl4::Get(id)
      .ReadFrom(&avio_mmio_)
      .set_BYPASS(1)
      .WriteTo(&avio_mmio_);

  // PLL power down.
  avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl3::Get(id)
      .ReadFrom(&avio_mmio_)
      .set_PDDP(1)
      .WriteTo(&avio_mmio_);

  if (frac != 0) {
    avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl::Get(id)
        .ReadFrom(&avio_mmio_)
        .set_RESETN(0)
        .WriteTo(&avio_mmio_);
    avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl1::Get(id)
        .ReadFrom(&avio_mmio_)
        .set_FRAC(frac)
        .WriteTo(&avio_mmio_);
  }

  avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl::Get(id)
      .ReadFrom(&avio_mmio_)
      .set_DN(dn)
      .set_DM(dm)
      .WriteTo(&avio_mmio_);
  avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl3::Get(id)
      .ReadFrom(&avio_mmio_)
      .set_DP(dp)
      .WriteTo(&avio_mmio_);
  zx_nanosleep(zx_deadline_after(ZX_USEC(2)));

  if (frac != 0) {
    avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl::Get(id)
        .ReadFrom(&avio_mmio_)
        .set_RESETN(1)
        .WriteTo(&avio_mmio_);
  }

  avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl3::Get(id)
      .ReadFrom(&avio_mmio_)
      .set_PDDP(0)
      .WriteTo(&avio_mmio_);
  // TODO(andresoportus): Wait for PLL lock instead of arbitrary delay.
  zx_nanosleep(zx_deadline_after(ZX_USEC(100)));

  avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl4::Get(id)
      .ReadFrom(&avio_mmio_)
      .set_BYPASS(0)
      .WriteTo(&avio_mmio_);
  if (avpll0) {
    avioGbl_AVPLLA_CLK_EN::Get().ReadFrom(&avio_mmio_).set_ctrl_AVPLL0(1).WriteTo(&avio_mmio_);
  } else {
    avioGbl_AVPLLA_CLK_EN::Get().ReadFrom(&avio_mmio_).set_ctrl_AVPLL1(1).WriteTo(&avio_mmio_);
  }
  return ZX_OK;
}

zx_status_t As370Clk::ClockImplEnable(uint32_t index) {
  switch (index) {
    case as370::kClkAvpll0:
      return AvpllClkEnable(true, true);
    case as370::kClkAvpll1:
      return AvpllClkEnable(false, true);
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t As370Clk::ClockImplDisable(uint32_t index) {
  switch (index) {
    case as370::kClkAvpll0:
      return AvpllClkEnable(true, false);
    case as370::kClkAvpll1:
      return AvpllClkEnable(false, false);
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t As370Clk::ClockImplIsEnabled(uint32_t id, bool* out_enabled) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t As370Clk::ClockImplQuerySupportedRate(uint32_t id, uint64_t max_rate,
                                                  uint64_t* out_best_rate) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t As370Clk::ClockImplGetRate(uint32_t id, uint64_t* out_current_rate) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t As370Clk::ClockImplSetRate(uint32_t index, uint64_t hz) {
  switch (index) {
    case as370::kClkAvpll0:
      return AvpllSetRate(true, hz);
    case as370::kClkAvpll1:
      return AvpllSetRate(false, hz);
    case as370::kClkCpu:
      return CpuSetRate(hz);
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t As370Clk::ClockImplSetInput(uint32_t id, uint32_t idx) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t As370Clk::ClockImplGetNumInputs(uint32_t id, uint32_t* out) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t As370Clk::ClockImplGetInput(uint32_t id, uint32_t* out) { return ZX_ERR_NOT_SUPPORTED; }

void As370Clk::DdkUnbind(ddk::UnbindTxn txn) {
  fbl::AutoLock lock(&lock_);

  global_mmio_.reset();
  avio_mmio_.reset();
  cpu_mmio_.reset();

  txn.Reply();
}

void As370Clk::DdkRelease() { delete this; }

}  // namespace clk

static constexpr zx_driver_ops_t syn_clk_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = clk::As370Clk::Create;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(syn_clk, syn_clk_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_SYNAPTICS),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AS370_CLOCK),
ZIRCON_DRIVER_END(syn_clk)
    // clang-format on
