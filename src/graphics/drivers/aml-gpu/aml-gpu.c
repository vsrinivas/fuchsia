// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/gpu/clock/c/fidl.h>
#include <lib/device-protocol/platform-device.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

#include "s905d2-gpu.h"
#include "s912-gpu.h"
#include "t931-gpu.h"

zx_status_t aml_gp0_init(aml_gpu_t* gpu);
void aml_gp0_release(aml_gpu_t* gpu);

static int32_t current_clk_source;

static void aml_gpu_set_clk_freq_source(aml_gpu_t* gpu, int32_t clk_source) {
  if (current_clk_source == clk_source) {
    return;
  }

  aml_gpu_block_t* gpu_block = gpu->gpu_block;
  GPU_INFO("Setting clock source to %d: %d\n", clk_source, gpu_block->gpu_clk_freq[clk_source]);
  uint32_t current_clk_cntl = READ32_HIU_REG(gpu_block->hhi_clock_cntl_offset);
  uint32_t enabled_mux = current_clk_cntl & (1 << FINAL_MUX_BIT_SHIFT);
  uint32_t new_mux = enabled_mux == 0;
  uint32_t mux_shift = new_mux ? 16 : 0;

  // clear existing values
  current_clk_cntl &= ~(CLOCK_MUX_MASK << mux_shift);
  // set the divisor, enable & source for the unused mux
  current_clk_cntl |= CALCULATE_CLOCK_MUX(true, gpu_block->gpu_clk_freq[clk_source], 1)
                      << mux_shift;

  // Write the new values to the unused mux
  WRITE32_HIU_REG(gpu_block->hhi_clock_cntl_offset, current_clk_cntl);
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  // Toggle current mux selection
  current_clk_cntl ^= (1 << FINAL_MUX_BIT_SHIFT);

  // Select the unused input mux
  WRITE32_HIU_REG(gpu_block->hhi_clock_cntl_offset, current_clk_cntl);

  current_clk_source = clk_source;
}

static void aml_gpu_set_initial_clk_freq_source(aml_gpu_t* gpu, int32_t clk_source) {
  aml_gpu_block_t* gpu_block = gpu->gpu_block;
  uint32_t current_clk_cntl = READ32_HIU_REG(gpu_block->hhi_clock_cntl_offset);
  uint32_t enabled_mux = (current_clk_cntl & (1 << FINAL_MUX_BIT_SHIFT)) != 0;
  uint32_t mux_shift = enabled_mux ? 16 : 0;

  if (current_clk_cntl & (1 << (mux_shift + CLK_ENABLED_BIT_SHIFT))) {
    aml_gpu_set_clk_freq_source(gpu, clk_source);
  } else {
    GPU_INFO("Setting initial clock source to %d: %d\n", clk_source,
             gpu_block->gpu_clk_freq[clk_source]);
    // Switching the final dynamic mux from a disabled source to an enabled
    // source doesn't work. If the current clock source is disabled, then
    // enable it instead of switching.
    current_clk_cntl &= ~(CLOCK_MUX_MASK << mux_shift);
    current_clk_cntl |= CALCULATE_CLOCK_MUX(true, gpu_block->gpu_clk_freq[clk_source], 1)
                        << mux_shift;

    // Write the new values to the existing mux.
    WRITE32_HIU_REG(gpu_block->hhi_clock_cntl_offset, current_clk_cntl);
    zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
    current_clk_source = clk_source;
  }
}

static void aml_gpu_init(aml_gpu_t* gpu) {
  uint32_t temp;
  aml_gpu_block_t* gpu_block = gpu->gpu_block;

  temp = READ32_PRESET_REG(gpu_block->reset0_mask_offset);
  temp &= ~(1 << 20);
  WRITE32_PRESET_REG(gpu_block->reset0_mask_offset, temp);

  temp = READ32_PRESET_REG(gpu_block->reset0_level_offset);
  temp &= ~(1 << 20);
  WRITE32_PRESET_REG(gpu_block->reset0_level_offset, temp);

  temp = READ32_PRESET_REG(gpu_block->reset2_mask_offset);
  temp &= ~(1 << 14);
  WRITE32_PRESET_REG(gpu_block->reset2_mask_offset, temp);

  temp = READ32_PRESET_REG(gpu_block->reset2_level_offset);
  temp &= ~(1 << 14);
  WRITE32_PRESET_REG(gpu_block->reset2_level_offset, temp);

  // Currently the index 2 corresponds to the default
  // value of GPU clock freq which is 500Mhz.
  // In future, the GPU driver in garnet
  // can make an IOCTL to set the default freq
  aml_gpu_set_initial_clk_freq_source(gpu, 2);

  temp = READ32_PRESET_REG(gpu_block->reset0_level_offset);
  temp |= 1 << 20;
  WRITE32_PRESET_REG(gpu_block->reset0_level_offset, temp);

  temp = READ32_PRESET_REG(gpu_block->reset2_level_offset);
  temp |= 1 << 14;
  WRITE32_PRESET_REG(gpu_block->reset2_level_offset, temp);

  WRITE32_GPU_REG(PWR_KEY, 0x2968A819);
  WRITE32_GPU_REG(PWR_OVERRIDE1, 0xfff | (0x20 << 16));
}

static void aml_gpu_release(void* ctx) {
  aml_gpu_t* gpu = ctx;
  aml_gp0_release(gpu);
  mmio_buffer_release(&gpu->hiu_buffer);
  mmio_buffer_release(&gpu->preset_buffer);
  mmio_buffer_release(&gpu->gpu_buffer);
  zx_handle_close(gpu->bti);
  free(gpu);
}

static zx_status_t aml_gpu_get_protocol(void* ctx, uint32_t proto_id, void* out_proto) {
  aml_gpu_t* gpu = ctx;
  pdev_protocol_t* gpu_proto = out_proto;

  // Forward the underlying ops.
  gpu_proto->ops = gpu->pdev.ops;
  gpu_proto->ctx = gpu->pdev.ctx;
  return ZX_OK;
}

static zx_status_t aml_gpu_SetFrequencySource(void* ctx, uint32_t clk_source, fidl_txn_t* txn) {
  aml_gpu_t* gpu = ctx;
  if (clk_source >= MAX_GPU_CLK_FREQ) {
    GPU_ERROR("Invalid clock freq source index\n");
    return fuchsia_hardware_gpu_clock_ClockSetFrequencySource_reply(txn, ZX_ERR_NOT_SUPPORTED);
  }
  aml_gpu_set_clk_freq_source(gpu, clk_source);
  return fuchsia_hardware_gpu_clock_ClockSetFrequencySource_reply(txn, ZX_OK);
}

static fuchsia_hardware_gpu_clock_Clock_ops_t fidl_ops = {.SetFrequencySource =
                                                              aml_gpu_SetFrequencySource};

static zx_status_t aml_gpu_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_gpu_clock_Clock_dispatch(ctx, txn, msg, &fidl_ops);
}

static zx_protocol_device_t aml_gpu_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = aml_gpu_release,
    .get_protocol = aml_gpu_get_protocol,
    .message = aml_gpu_message,
};

static zx_status_t aml_gpu_bind(void* ctx, zx_device_t* parent) {
  zx_status_t status;
  aml_gpu_t* gpu = calloc(1, sizeof(aml_gpu_t));
  if (!gpu) {
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &gpu->pdev)) != ZX_OK) {
    GPU_ERROR("ZX_PROTOCOL_PDEV not available\n");
    goto fail;
  }

  status = pdev_get_bti(&gpu->pdev, 0, &gpu->bti);
  if (status != ZX_OK) {
    GPU_ERROR("could not get BTI handle: %d\n", status);
    return status;
  }

  status =
      pdev_map_mmio_buffer(&gpu->pdev, MMIO_GPU, ZX_CACHE_POLICY_UNCACHED_DEVICE, &gpu->gpu_buffer);
  if (status != ZX_OK) {
    GPU_ERROR("pdev_map_mmio_buffer failed\n");
    goto fail;
  }

  status =
      pdev_map_mmio_buffer(&gpu->pdev, MMIO_HIU, ZX_CACHE_POLICY_UNCACHED_DEVICE, &gpu->hiu_buffer);
  if (status != ZX_OK) {
    GPU_ERROR("pdev_map_mmio_buffer failed\n");
    goto fail;
  }

  status = pdev_map_mmio_buffer(&gpu->pdev, MMIO_PRESET, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                &gpu->preset_buffer);
  if (status != ZX_OK) {
    GPU_ERROR("pdev_map_mmio_buffer failed\n");
    goto fail;
  }

  pdev_device_info_t info;
  status = pdev_get_device_info(&gpu->pdev, &info);
  if (status != ZX_OK) {
    GPU_ERROR("pdev_get_device_info failed\n");
    goto fail;
  }

  switch (info.pid) {
    case PDEV_PID_AMLOGIC_S912:
      gpu->gpu_block = &s912_gpu_blocks;
      break;
    case PDEV_PID_AMLOGIC_S905D2:
      gpu->gpu_block = &s905d2_gpu_blocks;
      break;
    case PDEV_PID_AMLOGIC_T931:
      gpu->gpu_block = &t931_gpu_blocks;
      break;
    default:
      GPU_ERROR("unsupported SOC PID %u\n", info.pid);
      goto fail;
  }

  if (info.pid == PDEV_PID_AMLOGIC_S905D2) {
    status = aml_gp0_init(gpu);
    if (status != ZX_OK) {
      GPU_ERROR("aml_gp0_init failed: %d\n", status);
      goto fail;
    }
  }

  aml_gpu_init(gpu);

  zx_device_prop_t props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_PDEV},
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_ARM_MALI},
  };

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "aml-gpu",
      .ctx = gpu,
      .ops = &aml_gpu_protocol,
      .props = props,
      .prop_count = countof(props),
      .proto_id = ZX_PROTOCOL_GPU_THERMAL,
  };

  status = device_add(parent, &args, &gpu->zxdev);
  if (status != ZX_OK) {
    goto fail;
  }

  return ZX_OK;

fail:
  aml_gpu_release(gpu);
  return status;
}

static zx_driver_ops_t aml_gpu_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = aml_gpu_bind,
};

ZIRCON_DRIVER_BEGIN(aml_gpu, aml_gpu_driver_ops, "zircon", "0.1", 6)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_MALI_INIT),
    // we support multiple SOC variants
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S912),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_T931), ZIRCON_DRIVER_END(aml_gpu)
