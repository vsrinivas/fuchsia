// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-gpu.h"

#include <fuchsia/hardware/gpu/clock/c/fidl.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/fidl-utils/bind.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/iommu.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <hw/reg.h>
#include <soc/aml-s905d2/s905d2-hiu.h>

#include "s905d2-gpu.h"
#include "s912-gpu.h"
#include "t931-gpu.h"

namespace aml_gpu {
AmlGpu::AmlGpu(zx_device_t* parent) : DdkDeviceType(parent) {}

AmlGpu::~AmlGpu() {}

void AmlGpu::SetClkFreqSource(int32_t clk_source) {
  if (current_clk_source_ == clk_source) {
    return;
  }

  GPU_INFO("Setting clock source to %d: %d\n", clk_source, gpu_block_->gpu_clk_freq[clk_source]);
  uint32_t current_clk_cntl = hiu_buffer_->Read32(4 * gpu_block_->hhi_clock_cntl_offset);
  uint32_t enabled_mux = current_clk_cntl & (1 << kFinalMuxBitShift);
  uint32_t new_mux = enabled_mux == 0;
  uint32_t mux_shift = new_mux ? 16 : 0;

  // clear existing values
  current_clk_cntl &= ~(kClockMuxMask << mux_shift);
  // set the divisor, enable & source for the unused mux
  current_clk_cntl |= CalculateClockMux(true, gpu_block_->gpu_clk_freq[clk_source], 1) << mux_shift;

  // Write the new values to the unused mux
  hiu_buffer_->Write32(current_clk_cntl, 4 * gpu_block_->hhi_clock_cntl_offset);
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  // Toggle current mux selection
  current_clk_cntl ^= (1 << kFinalMuxBitShift);

  // Select the unused input mux
  hiu_buffer_->Write32(current_clk_cntl, 4 * gpu_block_->hhi_clock_cntl_offset);

  current_clk_source_ = clk_source;
}

void AmlGpu::SetInitialClkFreqSource(int32_t clk_source) {
  uint32_t current_clk_cntl = hiu_buffer_->Read32(4 * gpu_block_->hhi_clock_cntl_offset);
  uint32_t enabled_mux = (current_clk_cntl & (1 << kFinalMuxBitShift)) != 0;
  uint32_t mux_shift = enabled_mux ? 16 : 0;

  if (current_clk_cntl & (1 << (mux_shift + kClkEnabledBitShift))) {
    SetClkFreqSource(clk_source);
  } else {
    GPU_INFO("Setting initial clock source to %d: %d\n", clk_source,
             gpu_block_->gpu_clk_freq[clk_source]);
    // Switching the final dynamic mux from a disabled source to an enabled
    // source doesn't work. If the current clock source is disabled, then
    // enable it instead of switching.
    current_clk_cntl &= ~(kClockMuxMask << mux_shift);
    current_clk_cntl |= CalculateClockMux(true, gpu_block_->gpu_clk_freq[clk_source], 1)
                        << mux_shift;

    // Write the new values to the existing mux.
    hiu_buffer_->Write32(current_clk_cntl, 4 * gpu_block_->hhi_clock_cntl_offset);
    zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
    current_clk_source_ = clk_source;
  }
}

zx_status_t AmlGpu::Gp0Init() {
  hiu_dev_ = std::make_unique<aml_hiu_dev_t>();
  gp0_pll_dev_ = std::make_unique<aml_pll_dev_t>();

  // HIU Init.
  zx_status_t status = s905d2_hiu_init(hiu_dev_.get());
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml_gp0_init: hiu_init failed: %d", status);
    return status;
  }

  status = s905d2_pll_init(hiu_dev_.get(), gp0_pll_dev_.get(), GP0_PLL);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml_gp0_init: pll_init failed: %d", status);
    return status;
  }

  status = s905d2_pll_set_rate(gp0_pll_dev_.get(), 846000000);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml_gp0_init: pll_set_rate failed: %d", status);
    return status;
  }
  status = s905d2_pll_ena(gp0_pll_dev_.get());
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml_gp0_init: pll_ena failed: %d", status);
    return status;
  }
  return ZX_OK;
}

void AmlGpu::InitClock() {
  uint32_t temp;

  temp = preset_buffer_->Read32(gpu_block_->reset0_mask_offset);
  temp &= ~(1 << 20);
  preset_buffer_->Write32(temp, gpu_block_->reset0_mask_offset);

  temp = preset_buffer_->Read32(gpu_block_->reset0_level_offset);
  temp &= ~(1 << 20);
  preset_buffer_->Write32(temp, gpu_block_->reset0_level_offset);

  temp = preset_buffer_->Read32(gpu_block_->reset2_mask_offset);
  temp &= ~(1 << 14);
  preset_buffer_->Write32(temp, gpu_block_->reset2_mask_offset);

  temp = preset_buffer_->Read32(gpu_block_->reset2_level_offset);
  temp &= ~(1 << 14);
  preset_buffer_->Write32(temp, gpu_block_->reset2_level_offset);

  SetInitialClkFreqSource(gpu_block_->initial_clock_index);

  temp = preset_buffer_->Read32(gpu_block_->reset0_level_offset);
  temp |= 1 << 20;
  preset_buffer_->Write32(temp, gpu_block_->reset0_level_offset);

  temp = preset_buffer_->Read32(gpu_block_->reset2_level_offset);
  temp |= 1 << 14;
  preset_buffer_->Write32(temp, gpu_block_->reset2_level_offset);

  gpu_buffer_->Write32(0x2968A819, 4 * kPwrKey);
  gpu_buffer_->Write32(0xfff | (0x20 << 16), 4 * kPwrOverride1);
}

zx_status_t AmlGpu::DdkGetProtocol(uint32_t proto_id, void* out_proto) {
  pdev_protocol_t* gpu_proto = static_cast<pdev_protocol_t*>(out_proto);
  // Forward the underlying ops.
  pdev_.GetProto(gpu_proto);
  return ZX_OK;
}

zx_status_t AmlGpu::SetFrequencySource(uint32_t clk_source, fidl_txn_t* txn) {
  if (clk_source >= kMaxGpuClkFreq) {
    GPU_ERROR("Invalid clock freq source index\n");
    return fuchsia_hardware_gpu_clock_ClockSetFrequencySource_reply(txn, ZX_ERR_NOT_SUPPORTED);
  }
  SetClkFreqSource(clk_source);
  return fuchsia_hardware_gpu_clock_ClockSetFrequencySource_reply(txn, ZX_OK);
}

static fuchsia_hardware_gpu_clock_Clock_ops_t fidl_ops = {
    .SetFrequencySource = fidl::Binder<AmlGpu>::BindMember<&AmlGpu::SetFrequencySource>,
};

zx_status_t AmlGpu::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_gpu_clock_Clock_dispatch(this, txn, msg, &fidl_ops);
}

zx_status_t AmlGpu::Bind() {
  pdev_ = ddk::PDev(parent_);

  zx_status_t status = pdev_.MapMmio(MMIO_GPU, &gpu_buffer_);
  if (status != ZX_OK) {
    GPU_ERROR("pdev_map_mmio_buffer failed\n");
    return status;
  }

  status = pdev_.MapMmio(MMIO_HIU, &hiu_buffer_);
  if (status != ZX_OK) {
    GPU_ERROR("pdev_map_mmio_buffer failed\n");
    return status;
  }

  status = pdev_.MapMmio(MMIO_PRESET, &preset_buffer_);
  if (status != ZX_OK) {
    GPU_ERROR("pdev_map_mmio_buffer failed\n");
    return status;
  }

  pdev_device_info_t info;
  status = pdev_.GetDeviceInfo(&info);
  if (status != ZX_OK) {
    GPU_ERROR("pdev_get_device_info failed\n");
    return status;
  }

  switch (info.pid) {
    case PDEV_PID_AMLOGIC_S912:
      gpu_block_ = &s912_gpu_blocks;
      break;
    case PDEV_PID_AMLOGIC_S905D2:
      gpu_block_ = &s905d2_gpu_blocks;
      break;
    case PDEV_PID_AMLOGIC_T931:
      gpu_block_ = &t931_gpu_blocks;
      break;
    default:
      GPU_ERROR("unsupported SOC PID %u\n", info.pid);
      return ZX_ERR_INVALID_ARGS;
  }

  if (info.pid == PDEV_PID_AMLOGIC_S905D2) {
    status = Gp0Init();
    if (status != ZX_OK) {
      GPU_ERROR("aml_gp0_init failed: %d\n", status);
      return status;
    }
  }

  InitClock();

  zx_device_prop_t props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_PDEV},
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_ARM_MALI},
  };

  status = DdkAdd(ddk::DeviceAddArgs("aml-gpu").set_props(props));

  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t aml_gpu_bind(void* ctx, zx_device_t* parent) {
  auto aml_gpu = std::make_unique<AmlGpu>(parent);
  zx_status_t status = aml_gpu->Bind();
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-gpu error binding: %d", status);
    return status;
  }
  aml_gpu.release();
  return ZX_OK;
}

}  // namespace aml_gpu

static zx_driver_ops_t aml_gpu_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = aml_gpu::aml_gpu_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_gpu, aml_gpu_driver_ops, "zircon", "0.1", 6)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_MALI_INIT),
    // we support multiple SOC variants
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S912),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_T931),
ZIRCON_DRIVER_END(aml_gpu)
