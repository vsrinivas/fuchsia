// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "osd.h"
#include "vpp-regs.h"
#include "vpu-regs.h"
#include <ddk/debug.h>
#include <ddktl/device.h>

namespace astro_display {

#define READ32_VPU_REG(a)               vpu_mmio_->Read32(a)
#define WRITE32_VPU_REG(a, v)           vpu_mmio_->Write32(v, a)

namespace {
constexpr uint32_t VpuViuOsd1BlkCfgTblAddrShift = 16;
constexpr uint32_t VpuViuOsd1BlkCfgLittleEndian = (1 << 15);
constexpr uint32_t VpuViuOsd1BlkCfgOsdBlkMode32Bit = 5;
constexpr uint32_t VpuViuOsd1BlkCfgOsdBlkModeShift = 8;
constexpr uint32_t VpuViuOsd1BlkCfgColorMatrixArgb = 1;
constexpr uint32_t VpuViuOsd1BlkCfgColorMatrixShift = 2;
constexpr uint32_t VpuViuOsd1CtrlStat2ReplacedAlphaEn = (1 << 14);
constexpr uint32_t VpuViuOsd1CtrlStat2ReplacedAlphaShift = 6u;

constexpr uint32_t kOsdGlobalAlphaDef = 0xff;
constexpr uint32_t kHwOsdBlockEnable0 = 0x0001; // osd blk0 enable

// We use bicubic interpolation for scaling.
// TODO(payamm): Add support for other types of interpolation
unsigned int osd_filter_coefs_bicubic[] = {
    0x00800000, 0x007f0100, 0xff7f0200, 0xfe7f0300, 0xfd7e0500, 0xfc7e0600,
    0xfb7d0800, 0xfb7c0900, 0xfa7b0b00, 0xfa7a0dff, 0xf9790fff, 0xf97711ff,
    0xf87613ff, 0xf87416fe, 0xf87218fe, 0xf8701afe, 0xf76f1dfd, 0xf76d1ffd,
    0xf76b21fd, 0xf76824fd, 0xf76627fc, 0xf76429fc, 0xf7612cfc, 0xf75f2ffb,
    0xf75d31fb, 0xf75a34fb, 0xf75837fa, 0xf7553afa, 0xf8523cfa, 0xf8503ff9,
    0xf84d42f9, 0xf84a45f9, 0xf84848f8
};

} // namespace
zx_status_t Osd::Init(zx_device_t* parent) {
    if (initialized_) {
        return ZX_OK;
    }

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &pdev_);
    if (status != ZX_OK) {
        return status;
    }

    // Map vpu mmio used by the OSD object
    mmio_buffer_t mmio;
    status = pdev_map_mmio_buffer2(&pdev_, MMIO_VPU, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &mmio);
    if (status != ZX_OK) {
        DISP_ERROR("osd: Could not map VPU mmio\n");
        return status;
    }

    vpu_mmio_ = ddk::MmioBuffer(mmio);

    // OSD object is ready to be used.
    initialized_ = true;
    return ZX_OK;
}

void Osd::Disable(void) {
    ZX_DEBUG_ASSERT(initialized_);
    vpu_mmio_->ClearBits32(1 << 0, VPU_VIU_OSD1_CTRL_STAT);
}

void Osd::Enable(void) {
    ZX_DEBUG_ASSERT(initialized_);
    vpu_mmio_->SetBits32(1 << 0, VPU_VIU_OSD1_CTRL_STAT);
}

zx_status_t Osd::Configure() {
    // TODO(payamm): OSD for g12a is slightly different from gxl. Currently, uBoot enables
    // scaling and 16bit mode (565) and configures various layers based on that assumption.
    // Since we don't have a full end-to-end driver at this moment, we cannot simply turn off
    // scaling.
    // For now, we will only configure the OSD layer to use the new Canvas index,
    // and use 32-bit color.
    // Set to use BGRX instead of BGRA.
    vpu_mmio_->SetBits32(VpuViuOsd1CtrlStat2ReplacedAlphaEn |
                             (0xff << VpuViuOsd1CtrlStat2ReplacedAlphaShift),
                         VPU_VIU_OSD1_CTRL_STAT2);

    return ZX_OK;
}


void Osd::Flip(uint8_t idx) {
    uint32_t cfg_w0 = (idx << VpuViuOsd1BlkCfgTblAddrShift) |
        VpuViuOsd1BlkCfgLittleEndian |
        (VpuViuOsd1BlkCfgOsdBlkMode32Bit << VpuViuOsd1BlkCfgOsdBlkModeShift) |
        (VpuViuOsd1BlkCfgColorMatrixArgb << VpuViuOsd1BlkCfgColorMatrixShift);

    vpu_mmio_->Write32(cfg_w0, VPU_VIU_OSD1_BLK0_CFG_W0);
    Enable();
}

void Osd::DefaultSetup() {
    // osd blend ctrl
    WRITE32_REG(VPU, VIU_OSD_BLEND_CTRL,
        4 << 29|
        0 << 27| // blend2_premult_en
        1 << 26| // blend_din0 input to blend0
        0 << 25| // blend1_dout to blend2
        0 << 24| // blend1_din3 input to blend1
        1 << 20| // blend_din_en
        0 << 16| // din_premult_en
        1 << 0); // din_reoder_sel = OSD1

    // vpp osd1 blend ctrl
    WRITE32_REG(VPU, OSD1_BLEND_SRC_CTRL,
        (0 & 0xf) << 0 |
        (0 & 0x1) << 4 |
        (3 & 0xf) << 8 | // postbld_src3_sel
        (0 & 0x1) << 16| // postbld_osd1_premult
        (1 & 0x1) << 20);
    // vpp osd2 blend ctrl
    WRITE32_REG(VPU, OSD2_BLEND_SRC_CTRL,
        (0 & 0xf) << 0 |
        (0 & 0x1) << 4 |
        (0 & 0xf) << 8 | // postbld_src4_sel
        (0 & 0x1) << 16 | // postbld_osd2_premult
        (1 & 0x1) << 20);

    // used default dummy data
    WRITE32_REG(VPU, VIU_OSD_BLEND_DUMMY_DATA0,
        0x0 << 16 |
        0x0 << 8 |
        0x0);
    // used default dummy alpha data
    WRITE32_REG(VPU, VIU_OSD_BLEND_DUMMY_ALPHA,
        0x0  << 20 |
        0x0  << 11 |
        0x0);

    // osdx setting
    WRITE32_REG(VPU,
        VPU_VIU_OSD_BLEND_DIN0_SCOPE_H,
        (fb_width_ - 1) << 16);

    WRITE32_REG(VPU,
        VPU_VIU_OSD_BLEND_DIN0_SCOPE_V,
        (fb_height_ - 1) << 16);

    WRITE32_REG(VPU, VIU_OSD_BLEND_BLEND0_SIZE,
        fb_height_ << 16 |
        fb_width_);
    WRITE32_REG(VPU, VIU_OSD_BLEND_BLEND1_SIZE,
        fb_height_  << 16 |
        fb_width_);
    SET_BIT32(VPU, DOLBY_PATH_CTRL, 0x3, 2, 2);

    WRITE32_REG(VPU, VPP_OSD1_IN_SIZE,
        fb_height_ << 16 | fb_width_);

    // setting blend scope
    WRITE32_REG(VPU, VPP_OSD1_BLD_H_SCOPE,
        0 << 16 | (fb_width_ - 1));
    WRITE32_REG(VPU, VPP_OSD1_BLD_V_SCOPE,
        0 << 16 | (fb_height_ - 1));

    // Set geometry to normal mode
    uint32_t data32 = ((fb_width_ - 1) & 0xfff) << 16;
    WRITE32_REG(VPU, VPU_VIU_OSD1_BLK0_CFG_W3 , data32);
    data32 = ((fb_height_ - 1) & 0xfff) << 16;
    WRITE32_REG(VPU, VPU_VIU_OSD1_BLK0_CFG_W4, data32);

    WRITE32_REG(VPU, VPU_VIU_OSD1_BLK0_CFG_W1, ((fb_width_ - 1) & 0x1fff) << 16);
    WRITE32_REG(VPU, VPU_VIU_OSD1_BLK0_CFG_W2, ((fb_height_ - 1) & 0x1fff) << 16);

    // enable osd blk0
    SET_BIT32(VPU, VPU_VIU_OSD1_CTRL_STAT, kHwOsdBlockEnable0, 0, 4);
}

void Osd::EnableScaling(bool enable) {
    int hf_phase_step, vf_phase_step;
    int src_w, src_h, dst_w, dst_h;
    int bot_ini_phase;
    int vsc_ini_rcv_num, vsc_ini_rpt_p0_num;
    int hsc_ini_rcv_num, hsc_ini_rpt_p0_num;
    int hf_bank_len = 4;
    int vf_bank_len = 0;
    uint32_t data32 = 0x0;

    vf_bank_len = 4;
    hsc_ini_rcv_num = hf_bank_len;
    vsc_ini_rcv_num = vf_bank_len;
    hsc_ini_rpt_p0_num =
        (hf_bank_len / 2 - 1) > 0 ? (hf_bank_len / 2 - 1) : 0;
    vsc_ini_rpt_p0_num =
        (vf_bank_len / 2 - 1) > 0 ? (vf_bank_len / 2 - 1) : 0;
    src_w = fb_width_;
    src_h = fb_height_;
    dst_w = display_width_;
    dst_h = display_height_;

    data32 = 0x0;
    if (enable) {
        /* enable osd scaler */
        data32 |= 1 << 2; /* enable osd scaler */
        data32 |= 1 << 3; /* enable osd scaler path */
        WRITE32_REG(VPU, VPU_VPP_OSD_SC_CTRL0, data32);
    } else {
        /* disable osd scaler path */
        WRITE32_REG(VPU, VPU_VPP_OSD_SC_CTRL0, 0);
    }
    hf_phase_step = (src_w << 18) / dst_w;
    hf_phase_step = (hf_phase_step << 6);
    vf_phase_step = (src_h << 20) / dst_h;
    bot_ini_phase = 0;
    vf_phase_step = (vf_phase_step << 4);

    /* config osd scaler in/out hv size */
    data32 = 0x0;
    if (enable) {
        data32 = (((src_h - 1) & 0x1fff)
              | ((src_w - 1) & 0x1fff) << 16);
        WRITE32_REG(VPU, VPU_VPP_OSD_SCI_WH_M1, data32);
        data32 = (((display_width_ - 1) & 0xfff));
        WRITE32_REG(VPU, VPU_VPP_OSD_SCO_H_START_END, data32);
        data32 = (((display_height_ - 1) & 0xfff));
        WRITE32_REG(VPU, VPU_VPP_OSD_SCO_V_START_END, data32);
    }
    data32 = 0x0;
    if (enable) {
        data32 |= (vf_bank_len & 0x7)
              | ((vsc_ini_rcv_num & 0xf) << 3)
              | ((vsc_ini_rpt_p0_num & 0x3) << 8);
        data32 |= 1 << 24;
    }
    WRITE32_REG(VPU, VPU_VPP_OSD_VSC_CTRL0, data32);
    data32 = 0x0;
    if (enable) {
        data32 |= (hf_bank_len & 0x7)
              | ((hsc_ini_rcv_num & 0xf) << 3)
              | ((hsc_ini_rpt_p0_num & 0x3) << 8);
        data32 |= 1 << 22;
    }
    WRITE32_REG(VPU, VPU_VPP_OSD_HSC_CTRL0, data32);
    data32 = 0x0;
    if (enable) {
        data32 |= (bot_ini_phase & 0xffff) << 16;
        SET_BIT32(VPU,VPU_VPP_OSD_HSC_PHASE_STEP,
                      hf_phase_step, 0, 28);
        SET_BIT32(VPU,VPU_VPP_OSD_HSC_INI_PHASE, 0, 0, 16);
        SET_BIT32(VPU,VPU_VPP_OSD_VSC_PHASE_STEP,
                      vf_phase_step, 0, 28);
        WRITE32_REG(VPU, VPU_VPP_OSD_VSC_INI_PHASE, data32);
    }

}
void Osd::HwInit() {
    ZX_DEBUG_ASSERT(initialized_);
    // Setup VPP horizontal width
    WRITE32_REG(VPU, VPP_POSTBLEND_H_SIZE, display_width_);

    // init vpu fifo control register
    uint32_t regVal = READ32_REG(VPU, VPP_OFIFO_SIZE);
    regVal = 0xfff << 20;
    regVal |= (0xfff + 1);
    WRITE32_REG(VPU, VPP_OFIFO_SIZE, regVal);

    // init osd fifo control and set DDR request priority to be urgent
    regVal = 1;
    regVal |= 4 << 5; // hold_fifo_lines
    regVal |= 1 << 10; // burst_len_sel 3 = 64. This bit is split between 10 and 31
    regVal |= 2 << 22;
    regVal |= 2 << 24;
    regVal |= 1 << 31;
    regVal |= 32 << 12; // fifo_depth_val: 32*8 = 256
    WRITE32_REG(VPU, VPU_VIU_OSD1_FIFO_CTRL_STAT, regVal);
    WRITE32_REG(VPU, VPU_VIU_OSD2_FIFO_CTRL_STAT, regVal);

    SET_MASK32(VPU, VPP_MISC, VPP_POSTBLEND_EN);
    CLEAR_MASK32(VPU, VPP_MISC, VPP_PREBLEND_EN);
    // just disable osd to avoid booting hang up
    regVal = 0x1 << 0;
    regVal |= kOsdGlobalAlphaDef << 12;
    regVal |= (1 << 21);
    WRITE32_REG(VPU, VPU_VIU_OSD1_CTRL_STAT , regVal);
    WRITE32_REG(VPU, VPU_VIU_OSD2_CTRL_STAT , regVal);

    DefaultSetup();

    EnableScaling(true);

    // Apply scale coefficients
    SET_BIT32(VPU, VPU_VPP_OSD_SCALE_COEF_IDX, 0x0000, 0, 9);
    for (int i = 0; i < 33; i++) {
        WRITE32_REG(VPU, VPU_VPP_OSD_SCALE_COEF, osd_filter_coefs_bicubic[i]);
    }

    SET_BIT32(VPU, VPU_VPP_OSD_SCALE_COEF_IDX, 0x0100, 0, 9);
    for (int i = 0; i < 33; i++) {
        WRITE32_REG(VPU, VPU_VPP_OSD_SCALE_COEF, osd_filter_coefs_bicubic[i]);
    }

    // update blending
    WRITE32_REG(VPU, VPU_VPP_OSD1_BLD_H_SCOPE, display_width_ - 1);
    WRITE32_REG(VPU, VPU_VPP_OSD1_BLD_V_SCOPE, display_height_ - 1);
    WRITE32_REG(VPU, VPU_VPP_OUT_H_V_SIZE, display_width_ << 16 | display_height_);
}

#define REG_OFFSET (0x20 << 2)
void Osd::Dump() {
    ZX_DEBUG_ASSERT(initialized_);
    uint32_t reg = 0;
    uint32_t offset = 0;
    uint32_t index = 0;

    reg = VPU_VIU_VENC_MUX_CTRL;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VPP_MISC;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VPP_OFIFO_SIZE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VPP_HOLD_LINES;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));

    reg = VPU_OSD_PATH_MISC_CTRL;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VIU_OSD_BLEND_CTRL;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VIU_OSD_BLEND_DIN0_SCOPE_H;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VIU_OSD_BLEND_DIN0_SCOPE_V;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VIU_OSD_BLEND_DIN1_SCOPE_H;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VIU_OSD_BLEND_DIN1_SCOPE_V;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VIU_OSD_BLEND_DIN2_SCOPE_H;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VIU_OSD_BLEND_DIN2_SCOPE_V;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VIU_OSD_BLEND_DIN3_SCOPE_H;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VIU_OSD_BLEND_DIN3_SCOPE_V;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VIU_OSD_BLEND_DUMMY_DATA0;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VIU_OSD_BLEND_DUMMY_ALPHA;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VIU_OSD_BLEND_BLEND0_SIZE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VIU_OSD_BLEND_BLEND1_SIZE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));

    reg = VPU_VPP_OSD1_IN_SIZE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VPP_OSD1_BLD_H_SCOPE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VPP_OSD1_BLD_V_SCOPE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VPP_OSD2_BLD_H_SCOPE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VPP_OSD2_BLD_V_SCOPE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = OSD1_BLEND_SRC_CTRL;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = OSD2_BLEND_SRC_CTRL;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VPP_POSTBLEND_H_SIZE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VPP_OUT_H_V_SIZE;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));

    reg = VPU_VPP_OSD_SC_CTRL0;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VPP_OSD_SCI_WH_M1;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VPP_OSD_SCO_H_START_END;
    DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VPP_OSD_SCO_V_START_END;
    DISP_INFO("reg[0x%x]: 0x%08x\n\n", reg, READ32_REG(VPU, reg));
    reg = VPU_VPP_POSTBLEND_H_SIZE;
    DISP_INFO("reg[0x%x]: 0x%08x\n\n", reg, READ32_REG(VPU, reg));
    for (index = 0; index < 2; index++) {
        if (index == 1)
            offset = REG_OFFSET;
        reg = offset + VPU_VIU_OSD1_FIFO_CTRL_STAT;
        DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
        reg = offset + VPU_VIU_OSD1_CTRL_STAT;
        DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
        reg = offset + VPU_VIU_OSD1_BLK0_CFG_W0;
        DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
        reg = offset + VPU_VIU_OSD1_BLK0_CFG_W1;
        DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
        reg = offset + VPU_VIU_OSD1_BLK0_CFG_W2;
        DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
        reg = offset + VPU_VIU_OSD1_BLK0_CFG_W3;
        DISP_INFO("reg[0x%x]: 0x%08x\n", reg, READ32_REG(VPU, reg));
        reg = VPU_VIU_OSD1_BLK0_CFG_W4;
        if (index == 1)
            reg = VPU_VIU_OSD2_BLK0_CFG_W4;
        DISP_INFO("reg[0x%x]: 0x%08x\n\n", reg, READ32_REG(VPU, reg));
    }
}

} // namespace astro_display
