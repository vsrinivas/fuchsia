// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "disp-rdma.h"

#include <fbl/alloc_checker.h>

#include "registers-disp-rdma.h"

namespace mt8167s_display {

namespace {
constexpr int kIdleTimeout = 20000;
}  // namespace

zx_status_t DispRdma::Init(zx_device_t* parent) {
  if (initialized_) {
    return ZX_OK;
  }

  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev_);
  if (status != ZX_OK) {
    return status;
  }

  // Map Disp RDMA MMIO
  mmio_buffer_t mmio;
  status = pdev_map_mmio_buffer(&pdev_, MMIO_DISP_RDMA, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map DISP RDMA mmio\n");
    return status;
  }
  fbl::AllocChecker ac;
  disp_rdma_mmio_ = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mmio);
  if (!ac.check()) {
    DISP_ERROR("Could not map DISP RDMA MMIO\n");
    return ZX_ERR_NO_MEMORY;
  }

  // Get BTI from parent
  status = pdev_get_bti(&pdev_, 0, bti_.reset_and_get_address());
  if (status != ZX_OK) {
    DISP_ERROR("Could not get BTI handle\n");
    return status;
  }

  // DISP RDMA is ready to be used
  initialized_ = true;
  return ZX_OK;
}

void DispRdma::Reset() {
  ZX_DEBUG_ASSERT(initialized_);
  int timeout = kIdleTimeout;
  Stop();

  // Set Soft Reset Bit
  disp_rdma_mmio_->Write32(disp_rdma_mmio_->Read32(DISP_RDMA_GLOBAL_CON) | GLOBAL_CON_SOFT_RESET,
                           DISP_RDMA_GLOBAL_CON);

  while (((disp_rdma_mmio_->Read32(DISP_RDMA_GLOBAL_CON) & GLOBAL_CON_RESET_STATE_MASK) ==
          GLOBAL_CON_RESTE_STATE_IDLE) &&
         timeout--) {
    zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
  }
  ZX_ASSERT(timeout > 0);

  // Clear Soft Reset Bit
  disp_rdma_mmio_->Write32(disp_rdma_mmio_->Read32(DISP_RDMA_GLOBAL_CON) & ~GLOBAL_CON_SOFT_RESET,
                           DISP_RDMA_GLOBAL_CON);

  timeout = kIdleTimeout;
  while (((disp_rdma_mmio_->Read32(DISP_RDMA_GLOBAL_CON) & GLOBAL_CON_RESET_STATE_MASK) !=
          GLOBAL_CON_RESTE_STATE_IDLE) &&
         timeout--) {
    zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
  }
  ZX_ASSERT(timeout > 0);
}

void DispRdma::Start() {
  ZX_DEBUG_ASSERT(initialized_);
  disp_rdma_mmio_->Write32(disp_rdma_mmio_->Read32(DISP_RDMA_GLOBAL_CON) | GLOBAL_CON_ENGINE_EN,
                           DISP_RDMA_GLOBAL_CON);
}

void DispRdma::Stop() {
  ZX_DEBUG_ASSERT(initialized_);
  disp_rdma_mmio_->Write32(0, DISP_RDMA_GLOBAL_CON);
  disp_rdma_mmio_->Write32(0, DISP_RDMA_INT_ENABLE);
  disp_rdma_mmio_->Write32(0, DISP_RDMA_INT_STATUS);
}

zx_status_t DispRdma::Config() {
  ZX_DEBUG_ASSERT(initialized_);
  // This also disables matrix conversion since we are operating in direct link mode
  disp_rdma_mmio_->Write32(SIZE_CON0_WIDTH(width_), DISP_RDMA_SIZE_CON0);
  disp_rdma_mmio_->Write32(SIZE_CON1_HEIGHT(height_), DISP_RDMA_SIZE_CON1);

  // Clear a bunch of registers that are only relevant in Memory mode and not direct mode
  disp_rdma_mmio_->Write32(0, DISP_RDMA_MEM_CON);
  disp_rdma_mmio_->Write32(0, DISP_RDMA_MEM_SRC_PITCH);
  disp_rdma_mmio_->Write32(0, DISP_RDMA_MEM_START_ADDR);
  disp_rdma_mmio_->Write32(0, DISP_RDMA_INT_ENABLE);  // not using interrupts

  disp_rdma_mmio_->ClearBits32(FIFO_CON_CLEAR_MASK, DISP_RDMA_FIFO_CON);
  disp_rdma_mmio_->Write32(FIFO_CON_FIFO_THRESHOLD_DEFAULT | FIFO_CON_UNDERFLOW_EN |
                               disp_rdma_mmio_->Read32(DISP_RDMA_FIFO_CON),
                           DISP_RDMA_FIFO_CON);

  // magic number needed to setup ultra registers
  disp_rdma_mmio_->Write32(0x1a01356b, DISP_RDMA_MEM_GMC_SETTING_0);
  return ZX_OK;
}

void DispRdma::Dump() {
  ZX_DEBUG_ASSERT(initialized_);
  zxlogf(INFO, "Dumping DISP RDMA Registers\n");
  zxlogf(INFO, "######################\n\n");
  zxlogf(INFO, "DISP_RDMA_INT_ENABLE = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_INT_ENABLE));
  zxlogf(INFO, "DISP_RDMA_INT_STATUS = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_INT_STATUS));
  zxlogf(INFO, "DISP_RDMA_GLOBAL_CON = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_GLOBAL_CON));
  zxlogf(INFO, "DISP_RDMA_SIZE_CON0 = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_SIZE_CON0));
  zxlogf(INFO, "DISP_RDMA_SIZE_CON1 = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_SIZE_CON1));
  zxlogf(INFO, "DISP_RDMA_TARGET_LINE = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_TARGET_LINE));
  zxlogf(INFO, "DISP_RDMA_MEM_CON = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_MEM_CON));
  zxlogf(INFO, "DISP_RDMA_MEM_SRC_PITCH = 0x%x\n",
         disp_rdma_mmio_->Read32(DISP_RDMA_MEM_SRC_PITCH));
  zxlogf(INFO, "DISP_RDMA_MEM_GMC_SETTING_0 = 0x%x\n",
         disp_rdma_mmio_->Read32(DISP_RDMA_MEM_GMC_SETTING_0));
  zxlogf(INFO, "DISP_RDMA_MEM_SLOW_CON = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_MEM_SLOW_CON));
  zxlogf(INFO, "DISP_RDMA_MEM_GMC_SETTING_1 = 0x%x\n",
         disp_rdma_mmio_->Read32(DISP_RDMA_MEM_GMC_SETTING_1));
  zxlogf(INFO, "DISP_RDMA_FIFO_CON = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_FIFO_CON));
  zxlogf(INFO, "DISP_RDMA_FIFO_LOG = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_FIFO_LOG));
  zxlogf(INFO, "DISP_RDMA_C00 = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_C00));
  zxlogf(INFO, "DISP_RDMA_C01 = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_C01));
  zxlogf(INFO, "DISP_RDMA_C02 = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_C02));
  zxlogf(INFO, "DISP_RDMA_C10 = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_C10));
  zxlogf(INFO, "DISP_RDMA_C11 = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_C11));
  zxlogf(INFO, "DISP_RDMA_C12 = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_C12));
  zxlogf(INFO, "DISP_RDMA_C20 = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_C20));
  zxlogf(INFO, "DISP_RDMA_C21 = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_C21));
  zxlogf(INFO, "DISP_RDMA_C22 = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_C22));
  zxlogf(INFO, "DISP_RDMA_PRE_ADD_0 = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_PRE_ADD_0));
  zxlogf(INFO, "DISP_RDMA_PRE_ADD_1 = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_PRE_ADD_1));
  zxlogf(INFO, "DISP_RDMA_PRE_ADD_2 = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_PRE_ADD_2));
  zxlogf(INFO, "DISP_RDMA_POST_ADD_0 = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_POST_ADD_0));
  zxlogf(INFO, "DISP_RDMA_POST_ADD_1 = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_POST_ADD_1));
  zxlogf(INFO, "DISP_RDMA_POST_ADD_2 = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_POST_ADD_2));
  zxlogf(INFO, "DISP_RDMA_DUMMY = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_DUMMY));
  zxlogf(INFO, "DISP_RDMA_DEBUG_OUT_SEL = 0x%x\n",
         disp_rdma_mmio_->Read32(DISP_RDMA_DEBUG_OUT_SEL));
  zxlogf(INFO, "DISP_RDMA_BG_CON_0 = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_BG_CON_0));
  zxlogf(INFO, "DISP_RDMA_BG_CON_1 = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_BG_CON_1));
  zxlogf(INFO, "DISP_RDMA_THRESHOLD_FOR_SODI = 0x%x\n",
         disp_rdma_mmio_->Read32(DISP_RDMA_THRESHOLD_FOR_SODI));
  zxlogf(INFO, "DISP_RDMA_IN_P_CNT = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_IN_P_CNT));
  zxlogf(INFO, "DISP_RDMA_IN_LINE_CNT = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_IN_LINE_CNT));
  zxlogf(INFO, "DISP_RDMA_OUT_P_CNT = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_OUT_P_CNT));
  zxlogf(INFO, "DISP_RDMA_OUT_LINE_CNT = 0x%x\n", disp_rdma_mmio_->Read32(DISP_RDMA_OUT_LINE_CNT));
  zxlogf(INFO, "DISP_RDMA_MEM_START_ADDR = 0x%x\n",
         disp_rdma_mmio_->Read32(DISP_RDMA_MEM_START_ADDR));
  zxlogf(INFO, "######################\n\n");
}

}  // namespace mt8167s_display
