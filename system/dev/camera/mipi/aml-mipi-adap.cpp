// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-mipi-regs.h"
#include "aml-mipi.h"
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/camera.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>
#include <hw/reg.h>
#include <stdint.h>
#include <threads.h>
#include <zircon/types.h>

namespace camera {

namespace {

constexpr uint32_t kFrontEnd0Size = 0x400;
constexpr uint32_t kReaderSize = 0x100;
constexpr uint32_t kPixelSize = 0x100;
constexpr uint32_t kAlignSize = 0x200;

} // namespace

zx_status_t AmlMipiDevice::AdapFrontendInit(const mipi_adap_info_t* info) {
    // TODO(braval):    Add support for DDR_MODE & DOL_MODE
    auto frontend_reg = mipi_adap_mmio_->View(FRONTEND_BASE, kFrontEnd0Size);

    // release from reset
    frontend_reg.Write32(0x0, CSI2_CLK_RESET);
    // enable frontend module clock and disable auto clock gating
    frontend_reg.Write32(0x6, CSI2_CLK_RESET);

    if (info->mode == MIPI_MODES_DIR_MODE) {
        if (info->path == MIPI_PATH_PATH0) {
            // bit[0] 1:enable virtual channel 0
            frontend_reg.Write32(0x001f0001, CSI2_GEN_CTRL0);
        }
    } else {
        zxlogf(ERROR, "%s, unsupported mode.\n", __func__);
        return ZX_ERR_NOT_SUPPORTED;
    }

    // applicable only to Raw data, direct MEM path
    frontend_reg.Write32(0xffff0000, CSI2_X_START_END_MEM);
    frontend_reg.Write32(0xffff0000, CSI2_Y_START_END_MEM);

    // set frame size
    frontend_reg.Write32(0x00000780, CSI2_DDR_STRIDE_PIX);

    // enable vs_rise_isp interrupt & enable ddr_wdone interrupt
    frontend_reg.Write32(0x5, CSI2_INTERRUPT_CTRL_STAT);
    return ZX_OK;
}

zx_status_t AmlMipiDevice::AdapReaderInit(const mipi_adap_info_t* info) {
    // TODO(braval):    Add support for DDR_MODE & DOL_MODE
    auto reader_reg = mipi_adap_mmio_->View(RD_BASE, kReaderSize);

    if (info->mode == MIPI_MODES_DIR_MODE) {
        reader_reg.Write32(0x02d00078, MIPI_ADAPT_DDR_RD0_CNTL1);
        reader_reg.Write32(0xb5000005, MIPI_ADAPT_DDR_RD0_CNTL0);
    } else {
        zxlogf(ERROR, "%s, unsupported mode.\n", __func__);
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
}

zx_status_t AmlMipiDevice::AdapPixelInit(const mipi_adap_info_t* info) {
    // TODO(braval):    Add support for DDR_MODE & DOL_MODE
    auto pixel_reg = mipi_adap_mmio_->View(PIXEL_BASE, kPixelSize);

    if (info->mode == MIPI_MODES_DIR_MODE) {
        // default width 1280
        pixel_reg.Write32(0x8000a500, MIPI_ADAPT_PIXEL0_CNTL0);
        pixel_reg.Write32(0x80000808, MIPI_ADAPT_PIXEL0_CNTL1);
    } else {
        zxlogf(ERROR, "%s, unsupported mode.\n", __func__);
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
}

zx_status_t AmlMipiDevice::AdapAlignInit(const mipi_adap_info_t* info) {
    // TODO(braval):    Add support for DDR_MODE & DOL_MODE
    auto align_reg = mipi_adap_mmio_->View(ALIGN_BASE, kAlignSize);

    int width, height, alig_width, alig_height, val;
    width = info->resolution.width;
    height = info->resolution.height;
    alig_width = width + 40;   // hblank > 32 cycles
    alig_height = height + 60; // vblank > 48 lines
    val = width + 35;          // width < val < alig_width
    align_reg.ModifyBits32(alig_width, 0, 13, MIPI_ADAPT_ALIG_CNTL0);
    align_reg.ModifyBits32(alig_height, 16, 13, MIPI_ADAPT_ALIG_CNTL0);
    align_reg.ModifyBits32(width, 16, 13, MIPI_ADAPT_ALIG_CNTL1);
    align_reg.ModifyBits32(height, 16, 13, MIPI_ADAPT_ALIG_CNTL2);
    align_reg.ModifyBits32(val, 16, 13, MIPI_ADAPT_ALIG_CNTL8);
    align_reg.ModifyBits32(1, 31, 1, MIPI_ADAPT_ALIG_CNTL8);
    return ZX_OK;
}

zx_status_t AmlMipiDevice::MipiAdapInit(const mipi_adap_info_t* info) {

    // TODO(braval):    Add support for DDR_MODE & DOL_MODE

    // Reset the Frontend
    auto frontend_reg = mipi_adap_mmio_->View(FRONTEND_BASE, kFrontEnd0Size);
    frontend_reg.Write32(1, CSI2_CLK_RESET);
    frontend_reg.Write32(0, CSI2_CLK_RESET);

    // default setting : 720p & RAW12
    zx_status_t status = AdapFrontendInit(info);
    if (status != ZX_OK) {
        return status;
    }

    status = AdapReaderInit(info);
    if (status != ZX_OK) {
        return status;
    }

    status = AdapPixelInit(info);
    if (status != ZX_OK) {
        return status;
    }

    status = AdapAlignInit(info);
    if (status != ZX_OK) {
        return status;
    }

    return status;
}

void AmlMipiDevice::MipiAdapStart() {
    // TODO(braval): Implement this
}

void AmlMipiDevice::MipiAdapReset() {
    // TODO(braval): Implement this
}

} // namespace camera
