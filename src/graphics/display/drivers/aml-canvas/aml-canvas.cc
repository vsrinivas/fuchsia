// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-canvas.h"

#include <assert.h>
#include <lib/device-protocol/platform-device.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/pixelformat.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

#include "dmc-regs.h"

namespace aml_canvas {

zx_status_t AmlCanvas::AmlogicCanvasConfig(zx::vmo vmo, size_t offset, const canvas_info_t* info,
                                           uint8_t* canvas_idx) {
  if (!info || !canvas_idx) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t size = fbl::round_up<uint32_t, uint32_t>(
      (info->stride_bytes * info->height) + static_cast<uint32_t>(offset % PAGE_SIZE), PAGE_SIZE);
  uint32_t index;
  zx_paddr_t paddr;
  fbl::AutoLock al(&lock_);

  uint32_t height = info->height;
  uint32_t width = info->stride_bytes;

  if (!(info->wrap & CanvasLutDataHigh::kDmcCavYwrap)) {
    // The precise height of the canvas doesn't matter if wrapping isn't in
    // use (as long as the user doesn't try to read or write outside of
    // the defined area).
    height = fbl::round_up<uint32_t, uint32_t>(height, 8);
  }

  if (!IS_ALIGNED(height, 8) || !IS_ALIGNED(width, 8)) {
    CANVAS_ERROR("Height or width is not aligned\n");
    return ZX_ERR_INVALID_ARGS;
  }

  // find an unused canvas index
  for (index = 0; index < kNumCanvasEntries; index++) {
    if (!pmts_[index].is_valid()) {
      break;
    }
  }

  if (index == kNumCanvasEntries) {
    CANVAS_ERROR("All canvas indices are currently in use\n");
    return ZX_ERR_NOT_FOUND;
  }

  uint32_t pin_flags = ZX_BTI_CONTIGUOUS;
  if (info->flags & CANVAS_FLAGS_READ) {
    pin_flags |= ZX_BTI_PERM_READ;
  }
  if (info->flags & CANVAS_FLAGS_WRITE) {
    pin_flags |= ZX_BTI_PERM_WRITE;
  }

  zx::pmt pmt;
  zx_status_t status = bti_.pin(pin_flags, vmo, fbl::round_down<size_t, size_t>(offset, PAGE_SIZE),
                                size, &paddr, 1, &pmt);
  if (status != ZX_OK) {
    CANVAS_ERROR("zx_bti_pin failed %d \n", status);
    return status;
  }

  if (!IS_ALIGNED(paddr, 8)) {
    CANVAS_ERROR("Physical address is not aligned\n");
    status = ZX_ERR_INVALID_ARGS;
    pmt.unpin();
    return ZX_ERR_INVALID_ARGS;
  }
  pmts_[index].swap(pmt);

  zx_paddr_t start_addr = paddr + (offset % PAGE_SIZE);

  // set framebuffer address in DMC, read/modify/write
  auto data_low = CanvasLutDataLow::Get().ReadFrom(&dmc_regs_);
  data_low.SetDmcCavWidth(width >> 3);
  data_low.set_dmc_cav_addr(static_cast<unsigned int>(start_addr >> 3));
  data_low.WriteTo(&dmc_regs_);

  auto data_high = CanvasLutDataHigh::Get().ReadFrom(&dmc_regs_);
  data_high.SetDmcCavWidth(width >> 3);
  data_high.set_dmc_cav_height(height);
  data_high.set_dmc_cav_blkmode(info->blkmode);
  data_high.set_dmc_cav_xwrap(info->wrap & CanvasLutDataHigh::kDmcCavXwrap ? 1 : 0);
  data_high.set_dmc_cav_ywrap(info->wrap & CanvasLutDataHigh::kDmcCavYwrap ? 1 : 0);
  data_high.set_dmc_cav_endianness(info->endianness);
  data_high.WriteTo(&dmc_regs_);

  auto lut_addr = CanvasLutAddr::Get().ReadFrom(&dmc_regs_);
  lut_addr.set_dmc_cav_addr_index(index);
  lut_addr.set_dmc_cav_addr_wr(1);
  lut_addr.WriteTo(&dmc_regs_);

  // read a cbus to make sure last write finished
  CanvasLutDataHigh::Get().ReadFrom(&dmc_regs_);

  *canvas_idx = static_cast<uint8_t>(index);

  return status;
}

zx_status_t AmlCanvas::AmlogicCanvasFree(uint8_t canvas_idx) {
  fbl::AutoLock al(&lock_);

  if (!pmts_[canvas_idx].is_valid()) {
    CANVAS_ERROR("Freeing invalid canvas index: %d\n", canvas_idx);
    return ZX_ERR_INVALID_ARGS;
  } else {
    pmts_[canvas_idx].unpin();
    pmts_[canvas_idx].reset();
  }

  return ZX_OK;
}

void AmlCanvas::DdkRelease() {
  lock_.Acquire();
  for (uint32_t index = 0; index < kNumCanvasEntries; index++) {
    if (pmts_[index].is_valid()) {
      pmts_[index].unpin();
    }
  }
  lock_.Release();
  delete this;
}

void AmlCanvas::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

// static funtion to create the canvas object and initialize its members
zx_status_t AmlCanvas::Setup(zx_device_t* parent) {
  // Get device protocol
  ddk::PDev pdev(parent);
  if (!pdev.is_valid()) {
    CANVAS_ERROR("Could not get parent protocol\n");
    return ZX_ERR_NO_RESOURCES;
  }

  // Get BTI handle
  zx::bti bti;
  zx_status_t status = pdev.GetBti(0, &bti);
  if (status != ZX_OK) {
    CANVAS_ERROR("Could not get BTI handle\n");
    return status;
  }

  // Map all MMIOs
  std::optional<ddk::MmioBuffer> mmio;
  status = pdev.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    CANVAS_ERROR("Could not map DMC registers %d\n", status);
    return status;
  }

  fbl::AllocChecker ac;
  auto canvas = fbl::make_unique_checked<aml_canvas::AmlCanvas>(&ac, parent, *std::move(mmio),
                                                                std::move(bti));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = canvas->DdkAdd("aml-canvas", DEVICE_ADD_ALLOW_MULTI_COMPOSITE);
  if (status != ZX_OK) {
    CANVAS_ERROR("Could not add aml canvas device: %d\n", status);
    return status;
  }

  // devmgr is now in charge of the memory for canvas
  __UNUSED auto ptr = canvas.release();

  return status;
}

}  // namespace aml_canvas

namespace {

static zx_status_t aml_canvas_bind(void* ctx, zx_device_t* parent) {
  zx_status_t status = aml_canvas::AmlCanvas::Setup(parent);
  if (status != ZX_OK) {
    CANVAS_ERROR("Could not set up aml canvas device: %d\n", status);
  }

  return status;
}

static constexpr zx_driver_ops_t aml_canvas_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = aml_canvas_bind;
  return ops;
}();

}  // namespace

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_canvas, aml_canvas_driver_ops, "zircon", "0.1", 4)
  BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
  BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_CANVAS),
ZIRCON_DRIVER_END(aml_canvas)
