// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vs680-clk.h"

#include <lib/device-protocol/pdev.h>

#include <ddk/binding.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <soc/vs680/vs680-clk.h>

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

  std::unique_ptr<Vs680Clk> device(new Vs680Clk(parent, *std::move(chip_ctrl_mmio),
                                                *std::move(cpu_pll_mmio), *std::move(avio_mmio)));

  if ((status = device->DdkAdd("vs680-clk")) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed %d\n", __FILE__, status);
    return status;
  }

  __UNUSED auto _ = device.release();
  return ZX_OK;
}

zx_status_t Vs680Clk::ClockImplEnable(uint32_t id) {
  fbl::AutoLock lock(&lock_);
  if (id >= fbl::count_of(clocks_) || !clocks_[id]) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return clocks_[id]->Enable();
}

zx_status_t Vs680Clk::ClockImplDisable(uint32_t id) {
  fbl::AutoLock lock(&lock_);
  if (id >= fbl::count_of(clocks_) || !clocks_[id]) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return clocks_[id]->Disable();
}

zx_status_t Vs680Clk::ClockImplIsEnabled(uint32_t id, bool* out_enabled) {
  fbl::AutoLock lock(&lock_);
  if (id >= fbl::count_of(clocks_) || !clocks_[id]) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return clocks_[id]->IsEnabled(out_enabled);
}

zx_status_t Vs680Clk::ClockImplSetRate(uint32_t id, uint64_t hz) {
  fbl::AutoLock lock(&lock_);
  if (id >= fbl::count_of(clocks_) || !clocks_[id]) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  auto result = GetParentRate(id);
  if (result.is_error()) {
    return result.status_value();
  }

  return clocks_[id]->SetRate(result.value(), hz);
}

zx_status_t Vs680Clk::ClockImplQuerySupportedRate(uint32_t id, uint64_t hz, uint64_t* out_hz) {
  fbl::AutoLock lock(&lock_);
  if (id >= fbl::count_of(clocks_) || !clocks_[id]) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  auto result = GetParentRate(id);
  if (result.is_error()) {
    return result.status_value();
  }

  return clocks_[id]->QuerySupportedRate(result.value(), hz, out_hz);
}

zx_status_t Vs680Clk::ClockImplGetRate(uint32_t id, uint64_t* out_hz) {
  fbl::AutoLock lock(&lock_);
  if (id >= fbl::count_of(clocks_) || !clocks_[id]) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  auto result = GetRate(id);
  if (result.is_ok()) {
    *out_hz = result.value();
  }
  return result.status_value();
}

zx_status_t Vs680Clk::ClockImplSetInput(uint32_t id, uint32_t idx) {
  fbl::AutoLock lock(&lock_);
  if (id >= fbl::count_of(clocks_) || !clocks_[id]) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return clocks_[id]->SetInput(idx);
}

zx_status_t Vs680Clk::ClockImplGetNumInputs(uint32_t id, uint32_t* out_n) {
  fbl::AutoLock lock(&lock_);
  if (id >= fbl::count_of(clocks_) || !clocks_[id]) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return clocks_[id]->GetNumInputs(out_n);
}

zx_status_t Vs680Clk::ClockImplGetInput(uint32_t id, uint32_t* out_index) {
  fbl::AutoLock lock(&lock_);
  if (id >= fbl::count_of(clocks_) || !clocks_[id]) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return clocks_[id]->GetInput(out_index);
}

zx::status<uint64_t> Vs680Clk::GetRate(uint32_t id) {
  auto result = GetParentRate(id);
  if (result.is_error()) {
    return result;
  }

  uint64_t rate_hz;
  zx_status_t status = clocks_[id]->GetRate(result.value(), &rate_hz);
  if (status != ZX_OK) {
    return zx::error_status(status);
  }
  return zx::success(rate_hz);
}

zx::status<uint64_t> Vs680Clk::GetParentRate(uint32_t id) {
  auto result = clocks_[id]->GetInputId();
  if (result.is_error()) {
    return result;
  }

  if (result.value() >= vs680::kClockCount) {
    return zx::success(0);
  }

  return GetRate(result.value());
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
