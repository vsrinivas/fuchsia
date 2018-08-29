// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "osd.h"
#include <ddk/debug.h>
#include <ddktl/device.h>

namespace astro_display {

constexpr uint32_t VpuViuOsd1BlkCfgTblAddrShift = 16;
constexpr uint32_t VpuViuOsd1BlkCfgLittleEndian = (1 << 15);
constexpr uint32_t VpuViuOsd1BlkCfgOsdBlkMode32Bit = 5;
constexpr uint32_t VpuViuOsd1BlkCfgOsdBlkModeShift = 8;
constexpr uint32_t VpuViuOsd1BlkCfgColorMatrixArgb = 1;
constexpr uint32_t VpuViuOsd1BlkCfgColorMatrixShift = 2;
constexpr uint32_t VpuViuOsd1CtrlStat2ReplacedAlphaEn = (1 << 14);
constexpr uint32_t VpuViuOsd1CtrlStat2ReplacedAlphaShift = 6u;

zx_status_t Osd::Init(zx_device_t* parent) {
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &pdev_);

    if (status != ZX_OK) {
        return status;
    }

    // Map vpu mmio used by the OSD object
    status = pdev_map_mmio_buffer(&pdev_, MMIO_VPU, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &mmio_vpu_);
    if (status != ZX_OK) {
        DISP_ERROR("osd: Could not map VPU mmio\n");
        return status;
    }

    vpu_regs_ = fbl::make_unique<hwreg::RegisterIo>(io_buffer_virt(&mmio_vpu_));

    // OSD object is ready to be used.
    return ZX_OK;
}

void Osd::Disable(void) {
    uint32_t regVal = vpu_regs_->Read<uint32_t>(VPU_VIU_OSD1_CTRL_STAT);
    regVal &= ~(1 << 0);
    vpu_regs_->Write(VPU_VIU_OSD1_CTRL_STAT, regVal);
}

void Osd::Enable(void) {
    uint32_t regVal = vpu_regs_->Read<uint32_t>(VPU_VIU_OSD1_CTRL_STAT);
    regVal |= (1 << 0);
    vpu_regs_->Write(VPU_VIU_OSD1_CTRL_STAT, regVal);
}

zx_status_t Osd::Configure(void) {
    // TODO(payamm): OSD for g12a is slightly different from gxl. Currently, uBoot enables
    // scaling and 16bit mode (565) and configures various layers based on that assumption.
    // Since we don't have a full end-to-end driver at this moment, we cannot simply turn off
    // scaling.
    // For now, we will only configure the OSD layer to use the new Canvas index,
    // and use 32-bit color.
    uint32_t ctrl_stat2 = vpu_regs_->Read<uint32_t>(VPU_VIU_OSD1_CTRL_STAT2);
    ctrl_stat2 |= VpuViuOsd1CtrlStat2ReplacedAlphaEn |
                  (0xff << VpuViuOsd1CtrlStat2ReplacedAlphaShift);
    // Set to use BGRX instead of BGRA.
    vpu_regs_->Write(VPU_VIU_OSD1_CTRL_STAT2, ctrl_stat2);

    return ZX_OK;
}


void Osd::Flip(uint8_t idx) {
    uint32_t cfg_w0 = (idx << VpuViuOsd1BlkCfgTblAddrShift) |
        VpuViuOsd1BlkCfgLittleEndian |
        (VpuViuOsd1BlkCfgOsdBlkMode32Bit << VpuViuOsd1BlkCfgOsdBlkModeShift) |
        (VpuViuOsd1BlkCfgColorMatrixArgb << VpuViuOsd1BlkCfgColorMatrixShift);

    vpu_regs_->Write(VPU_VIU_OSD1_BLK0_CFG_W0, cfg_w0);
    Enable();
}

} // namespace astro_display
