// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "osd.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <zircon/errors.h>
#include <zircon/pixelformat.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <algorithm>
#include <cmath>

#include <ddk/debug.h>
#include <ddk/protocol/display/controller.h>
#include <ddktl/device.h>
#include <fbl/auto_lock.h>

#include "lib/zx/time.h"
#include "rdma-regs.h"
#include "src/graphics/display/drivers/amlogic-display/common.h"
#include "src/graphics/display/drivers/amlogic-display/hhi-regs.h"
#include "vpp-regs.h"
#include "vpu-regs.h"

namespace amlogic_display {

#define READ32_VPU_REG(a) vpu_mmio_->Read32(a)
#define WRITE32_VPU_REG(a, v) vpu_mmio_->Write32(v, a)

namespace {
constexpr uint32_t VpuViuOsd1BlkCfgTblAddrShift = 16;
constexpr uint32_t VpuViuOsd1BlkCfgLittleEndian = (1 << 15);
constexpr uint32_t VpuViuOsd1BlkCfgOsdBlkMode32Bit = 5;
constexpr uint32_t VpuViuOsd1BlkCfgOsdBlkModeShift = 8;
constexpr uint32_t VpuViuOsd1BlkCfgColorMatrixArgb = 1;
constexpr uint32_t VpuViuOsd1BlkCfgColorMatrixShift = 2;

constexpr uint32_t VpuViuOsd1CtrlStatOsdBlkEnable = (1 << 0);

constexpr uint32_t VpuViuOsd1CtrlStat2ReplacedAlphaEn = (1 << 14);
constexpr uint32_t VpuViuOsd1CtrlStat2ReplacedAlphaShift = 6u;

constexpr uint32_t kMaximumAlpha = 0xff;
constexpr uint32_t kOsdGlobalAlphaShift = 12;
constexpr uint32_t kOsdGlobalAlphaMask = (0x1FF << kOsdGlobalAlphaShift);
constexpr uint32_t kHwOsdBlockEnable0 = 0x0001;  // osd blk0 enable

// We use bicubic interpolation for scaling.
// TODO(payamm): Add support for other types of interpolation
unsigned int osd_filter_coefs_bicubic[] = {
    0x00800000, 0x007f0100, 0xff7f0200, 0xfe7f0300, 0xfd7e0500, 0xfc7e0600, 0xfb7d0800,
    0xfb7c0900, 0xfa7b0b00, 0xfa7a0dff, 0xf9790fff, 0xf97711ff, 0xf87613ff, 0xf87416fe,
    0xf87218fe, 0xf8701afe, 0xf76f1dfd, 0xf76d1ffd, 0xf76b21fd, 0xf76824fd, 0xf76627fc,
    0xf76429fc, 0xf7612cfc, 0xf75f2ffb, 0xf75d31fb, 0xf75a34fb, 0xf75837fa, 0xf7553afa,
    0xf8523cfa, 0xf8503ff9, 0xf84d42f9, 0xf84a45f9, 0xf84848f8};

constexpr uint32_t kFloatToFixed3_10ScaleFactor = 1024;
constexpr int32_t kMaxFloatToFixed3_10 = (4 * kFloatToFixed3_10ScaleFactor) - 1;
constexpr int32_t kMinFloatToFixed3_10 = -4 * kFloatToFixed3_10ScaleFactor;
constexpr uint32_t kFloatToFixed3_10Mask = 0x1FFF;

constexpr uint32_t kFloatToFixed2_10ScaleFactor = 1024;
constexpr int32_t kMaxFloatToFixed2_10 = (2 * kFloatToFixed2_10ScaleFactor) - 1;
constexpr int32_t kMinFloatToFixed2_10 = -2 * kFloatToFixed2_10ScaleFactor;
constexpr uint32_t kFloatToFixed2_10Mask = 0xFFF;

}  // namespace

int Osd::RdmaThread() {
  zx_status_t status;
  while (1) {
    status = rdma_irq_.wait(nullptr);
    if (status != ZX_OK) {
      DISP_ERROR("RDMA Interrupt wait failed\n");
      break;
    }
    // RDMA completed. Remove source for all finished DMA channels
    for (int i = 0; i < kMaxRdmaChannels; i++) {
      if (vpu_mmio_->Read32(VPU_RDMA_STATUS) & RDMA_STATUS_DONE(i)) {
        fbl::AutoLock lock(&rdma_lock_);
        uint32_t regVal = vpu_mmio_->Read32(VPU_RDMA_ACCESS_AUTO);
        regVal &= ~RDMA_ACCESS_AUTO_INT_EN(i);  // VSYNC interrupt source
        vpu_mmio_->Write32(regVal, VPU_RDMA_ACCESS_AUTO);
      }
    }
  }
  return status;
}

zx_status_t Osd::Init(zx_device_t* parent) {
  if (initialized_) {
    return ZX_OK;
  }

  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev_);
  if (status != ZX_OK) {
    return status;
  }

  // Map vpu mmio used by the OSD object
  mmio_buffer_t mmio;
  status = pdev_map_mmio_buffer(&pdev_, MMIO_VPU, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("osd: Could not map VPU mmio\n");
    return status;
  }

  vpu_mmio_ = ddk::MmioBuffer(mmio);

  // Get BTI from parent
  status = pdev_get_bti(&pdev_, 0, bti_.reset_and_get_address());
  if (status != ZX_OK) {
    DISP_ERROR("Could not get BTI handle\n");
    return status;
  }

  // Map RDMA Done Interrupt
  status = pdev_get_interrupt(&pdev_, IRQ_RDMA, 0, rdma_irq_.reset_and_get_address());
  if (status != ZX_OK) {
    DISP_ERROR("Could not map RDMA interrupt\n");
    return status;
  }

  auto start_thread = [](void* arg) { return static_cast<Osd*>(arg)->RdmaThread(); };
  status = thrd_create_with_name(&rdma_thread_, start_thread, this, "rdma_thread");
  if (status != ZX_OK) {
    DISP_ERROR("Could not create rdma_thread\n");
    return status;
  }

  // Setup RDMA
  status = SetupRdma();
  if (status != ZX_OK) {
    DISP_ERROR("Could not setup RDMA\n");
    return status;
  }

  // OSD object is ready to be used.
  initialized_ = true;
  return ZX_OK;
}

void Osd::Disable(void) {
  ZX_DEBUG_ASSERT(initialized_);
  // Display RDMA
  vpu_mmio_->ClearBits32(RDMA_ACCESS_AUTO_INT_EN_ALL, VPU_RDMA_ACCESS_AUTO);
  vpu_mmio_->ClearBits32(1 << 0, VPU_VIU_OSD1_CTRL_STAT);
}

void Osd::Enable(void) {
  ZX_DEBUG_ASSERT(initialized_);
  vpu_mmio_->SetBits32(1 << 0, VPU_VIU_OSD1_CTRL_STAT);
}

uint32_t Osd::FloatToFixed2_10(float f) {
  auto fixed_num = static_cast<int32_t>(round(f * kFloatToFixed2_10ScaleFactor));

  // Amlogic hardware accepts values [-2 2). Let's make sure the result is within this range.
  // If not, clamp it
  fixed_num = std::clamp(fixed_num, kMinFloatToFixed2_10, kMaxFloatToFixed2_10);
  return fixed_num & kFloatToFixed2_10Mask;
}

uint32_t Osd::FloatToFixed3_10(float f) {
  auto fixed_num = static_cast<int32_t>(round(f * kFloatToFixed3_10ScaleFactor));

  // Amlogic hardware accepts values [-4 4). Let's make sure the result is within this range.
  // If not, clamp it
  fixed_num = std::clamp(fixed_num, kMinFloatToFixed3_10, kMaxFloatToFixed3_10);
  return fixed_num & kFloatToFixed3_10Mask;
}

void Osd::FlipOnVsync(uint8_t idx, const display_config_t* config) {
  // Get the first available channel
  int rdma_channel = GetNextAvailableRdmaChannel();
  uint8_t retry_count = 0;
  while (rdma_channel == -1 && retry_count++ < kMaxRetries) {
    zx_nanosleep(zx_deadline_after(ZX_MSEC(8)));
    rdma_channel = GetNextAvailableRdmaChannel();
  }

  if (rdma_channel < 0) {
    DISP_ERROR("Could not find any available RDMA channels!\n");
    Dump();
    ZX_DEBUG_ASSERT(false);
    return;
  }

  DISP_SPEW("Channel used is %d\n", rdma_channel);

  if (config->gamma_table_present) {
    if (config->apply_gamma_table) {
      // Gamma Table needs to be programmed manually. Cannot use RDMA
      SetGamma(GammaChannel::kRed, config->gamma_red_list);
      SetGamma(GammaChannel::kGreen, config->gamma_green_list);
      SetGamma(GammaChannel::kBlue, config->gamma_blue_list);
    }
    // Enable Gamma at vsync using RDMA
    SetRdmaTableValue(rdma_channel, IDX_GAMMA_EN, 1);
  } else {
    // Disable Gamma at vsync using RDMA
    SetRdmaTableValue(rdma_channel, IDX_GAMMA_EN, 0);
  }

  // Update CFG_W0 with correct Canvas Index
  uint32_t cfg_w0 = (idx << VpuViuOsd1BlkCfgTblAddrShift) | VpuViuOsd1BlkCfgLittleEndian |
                    (VpuViuOsd1BlkCfgOsdBlkMode32Bit << VpuViuOsd1BlkCfgOsdBlkModeShift) |
                    (VpuViuOsd1BlkCfgColorMatrixArgb << VpuViuOsd1BlkCfgColorMatrixShift);
  SetRdmaTableValue(rdma_channel, IDX_CFG_W0, cfg_w0);

  auto primary_layer = config->layer_list[0]->cfg.primary;

  // Configure ctrl_stat and ctrl_stat2 registers
  uint32_t osd_ctrl_stat_val = vpu_mmio_->Read32(VPU_VIU_OSD1_CTRL_STAT);
  uint32_t osd_ctrl_stat2_val = vpu_mmio_->Read32(VPU_VIU_OSD1_CTRL_STAT2);

  // enable OSD Block
  osd_ctrl_stat_val |= VpuViuOsd1CtrlStatOsdBlkEnable;

  // Amlogic supports two types of alpha blending:
  // Global: This alpha value is applied to the entire plane (i.e. all pixels)
  // Per-Pixel: Each pixel will be multiplied by its corresponding alpha channel
  //
  // If alpha blending is disabled by the client or we are supporting a format that does
  // not have an alpha channel, we need to:
  // a) Set global alpha multiplier to 1 (i.e. 0xFF)
  // b) Enable "replaced_alpha" and set its value to 0xFF. This will effectively
  //    tell the hardware to replace the value found in alpha channel with the "replaced"
  //    value
  //
  // If alpha blending is enabled but alpha_layer_val is NaN:
  // - Set global alpha multiplier to 1 (i.e. 0xFF)
  // - Disable "replaced_alpha" which allows hardware to use per-pixel alpha channel.
  //
  // If alpha blending is enabled and alpha_layer_val has a value:
  // - Set global alpha multiplier to alpha_layer_val
  // - Disable "replaced_alpha" which allows hardware to use per-pixel alpha channel.

  // Load default values: Set global alpha to 1 and enable replaced_alpha.
  osd_ctrl_stat2_val |=
      (kMaximumAlpha << VpuViuOsd1CtrlStat2ReplacedAlphaShift) | VpuViuOsd1CtrlStat2ReplacedAlphaEn;
  osd_ctrl_stat_val |= kMaximumAlpha << kOsdGlobalAlphaShift;

  if (primary_layer.alpha_mode != ALPHA_DISABLE) {
    // If a global alpha value is provided, apply it.
    if (!isnan(primary_layer.alpha_layer_val)) {
      auto num = static_cast<uint8_t>(round(primary_layer.alpha_layer_val * kMaximumAlpha));
      osd_ctrl_stat_val &= ~(kOsdGlobalAlphaMask);
      osd_ctrl_stat_val |= (num << kOsdGlobalAlphaShift);
    }
    // If format includes alpha channel, disable "replaced_alpha"
    if (primary_layer.image.pixel_format != ZX_PIXEL_FORMAT_RGB_x888) {
      osd_ctrl_stat2_val &= ~(VpuViuOsd1CtrlStat2ReplacedAlphaEn);
    }
  }

  SetRdmaTableValue(rdma_channel, IDX_CTRL_STAT, osd_ctrl_stat_val);
  SetRdmaTableValue(rdma_channel, IDX_CTRL_STAT2, osd_ctrl_stat2_val);

  // Perform color correction if needed
  if (config->cc_flags) {
    // Set enable bit
    SetRdmaTableValue(rdma_channel, IDX_MATRIX_EN_CTRL,
                      vpu_mmio_->Read32(VPU_VPP_POST_MATRIX_EN_CTRL) | (1 << 0));

    // Load PreOffset values (or 0 if none entered)
    SetRdmaTableValue(rdma_channel, IDX_MATRIX_PRE_OFFSET0_1,
                      config->cc_flags & COLOR_CONVERSION_PREOFFSET
                          ? (FloatToFixed2_10(config->cc_preoffsets[0]) << 16 |
                             FloatToFixed2_10(config->cc_preoffsets[1]) << 0)
                          : 0);
    SetRdmaTableValue(rdma_channel, IDX_MATRIX_PRE_OFFSET2,
                      config->cc_flags & COLOR_CONVERSION_PREOFFSET
                          ? (FloatToFixed2_10(config->cc_preoffsets[2]) << 0)
                          : 0);

    // Load PostOffset values (or 0 if none entered)
    SetRdmaTableValue(rdma_channel, IDX_MATRIX_OFFSET0_1,
                      config->cc_flags & COLOR_CONVERSION_POSTOFFSET
                          ? (FloatToFixed2_10(config->cc_postoffsets[0]) << 16 |
                             FloatToFixed2_10(config->cc_postoffsets[1]) << 0)
                          : 0);
    SetRdmaTableValue(rdma_channel, IDX_MATRIX_OFFSET2,
                      config->cc_flags & COLOR_CONVERSION_POSTOFFSET
                          ? (FloatToFixed2_10(config->cc_postoffsets[2]) << 0)
                          : 0);

    float identity[3][3] = {
        {
            1,
            0,
            0,
        },
        {
            0,
            1,
            0,
        },
        {
            0,
            0,
            1,
        },
    };

    // This will include either the entered coefficient matrix or the identity matrix
    float final[3][3] = {};

    for (uint32_t i = 0; i < 3; i++) {
      for (uint32_t j = 0; j < 3; j++) {
        final[i][j] = config->cc_flags & COLOR_CONVERSION_COEFFICIENTS
                          ? config->cc_coefficients[i][j]
                          : identity[i][j];
      }
    }

    // Load up the coefficient matrix registers
    SetRdmaTableValue(rdma_channel, IDX_MATRIX_COEF00_01,
                      FloatToFixed3_10(final[0][0]) << 16 | FloatToFixed3_10(final[0][1]) << 0);
    SetRdmaTableValue(rdma_channel, IDX_MATRIX_COEF02_10,
                      FloatToFixed3_10(final[0][2]) << 16 | FloatToFixed3_10(final[1][0]) << 0);
    SetRdmaTableValue(rdma_channel, IDX_MATRIX_COEF11_12,
                      FloatToFixed3_10(final[1][1]) << 16 | FloatToFixed3_10(final[1][2]) << 0);
    SetRdmaTableValue(rdma_channel, IDX_MATRIX_COEF20_21,
                      FloatToFixed3_10(final[2][0]) << 16 | FloatToFixed3_10(final[2][1]) << 0);
    SetRdmaTableValue(rdma_channel, IDX_MATRIX_COEF22, FloatToFixed3_10(final[2][2]) << 0);
  } else {
    // Disable color conversion engine
    SetRdmaTableValue(rdma_channel, IDX_MATRIX_EN_CTRL,
                      vpu_mmio_->Read32(VPU_VPP_POST_MATRIX_EN_CTRL) & ~(1 << 0));
  }
  FlushRdmaTable(rdma_channel);

  // Write the start and end address of the table. End address is the last address that the
  // RDMA engine reads from.
  vpu_mmio_->Write32(static_cast<uint32_t>(rdma_chnl_container_[rdma_channel].phys_offset),
                     VPU_RDMA_AHB_START_ADDR(rdma_channel));
  vpu_mmio_->Write32(static_cast<uint32_t>(rdma_chnl_container_[rdma_channel].phys_offset +
                                           (sizeof(RdmaTable) * kRdmaTableMaxSize) - 4),
                     VPU_RDMA_AHB_END_ADDR(rdma_channel));

  // Enable Auto mode: Non-Increment, VSync Interrupt Driven, Write
  fbl::AutoLock lock(&rdma_lock_);
  uint32_t regVal = vpu_mmio_->Read32(VPU_RDMA_ACCESS_AUTO);
  regVal |= RDMA_ACCESS_AUTO_INT_EN(rdma_channel);  // VSYNC interrupt source
  regVal |= RDMA_ACCESS_AUTO_WRITE(rdma_channel);   // Write
  vpu_mmio_->Write32(regVal, VPU_RDMA_ACCESS_AUTO);
}

void Osd::DefaultSetup() {
  // osd blend ctrl
  WRITE32_REG(VPU, VIU_OSD_BLEND_CTRL,
              4 << 29 | 0 << 27 |  // blend2_premult_en
                  1 << 26 |        // blend_din0 input to blend0
                  0 << 25 |        // blend1_dout to blend2
                  0 << 24 |        // blend1_din3 input to blend1
                  1 << 20 |        // blend_din_en
                  0 << 16 |        // din_premult_en
                  1 << 0);         // din_reoder_sel = OSD1

  // vpp osd1 blend ctrl
  WRITE32_REG(VPU, OSD1_BLEND_SRC_CTRL,
              (0 & 0xf) << 0 | (0 & 0x1) << 4 | (3 & 0xf) << 8 |  // postbld_src3_sel
                  (0 & 0x1) << 16 |                               // postbld_osd1_premult
                  (1 & 0x1) << 20);
  // vpp osd2 blend ctrl
  WRITE32_REG(VPU, OSD2_BLEND_SRC_CTRL,
              (0 & 0xf) << 0 | (0 & 0x1) << 4 | (0 & 0xf) << 8 |  // postbld_src4_sel
                  (0 & 0x1) << 16 |                               // postbld_osd2_premult
                  (1 & 0x1) << 20);

  // used default dummy data
  WRITE32_REG(VPU, VIU_OSD_BLEND_DUMMY_DATA0, 0x0 << 16 | 0x0 << 8 | 0x0);
  // used default dummy alpha data
  WRITE32_REG(VPU, VIU_OSD_BLEND_DUMMY_ALPHA, 0x0 << 20 | 0x0 << 11 | 0x0);

  // osdx setting
  WRITE32_REG(VPU, VPU_VIU_OSD_BLEND_DIN0_SCOPE_H, (fb_width_ - 1) << 16);

  WRITE32_REG(VPU, VPU_VIU_OSD_BLEND_DIN0_SCOPE_V, (fb_height_ - 1) << 16);

  WRITE32_REG(VPU, VIU_OSD_BLEND_BLEND0_SIZE, fb_height_ << 16 | fb_width_);
  WRITE32_REG(VPU, VIU_OSD_BLEND_BLEND1_SIZE, fb_height_ << 16 | fb_width_);
  SET_BIT32(VPU, DOLBY_PATH_CTRL, 0x3, 2, 2);

  WRITE32_REG(VPU, VPP_OSD1_IN_SIZE, fb_height_ << 16 | fb_width_);

  // setting blend scope
  WRITE32_REG(VPU, VPP_OSD1_BLD_H_SCOPE, 0 << 16 | (fb_width_ - 1));
  WRITE32_REG(VPU, VPP_OSD1_BLD_V_SCOPE, 0 << 16 | (fb_height_ - 1));

  // Set geometry to normal mode
  uint32_t data32 = ((fb_width_ - 1) & 0xfff) << 16;
  WRITE32_REG(VPU, VPU_VIU_OSD1_BLK0_CFG_W3, data32);
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
  hsc_ini_rpt_p0_num = (hf_bank_len / 2 - 1) > 0 ? (hf_bank_len / 2 - 1) : 0;
  vsc_ini_rpt_p0_num = (vf_bank_len / 2 - 1) > 0 ? (vf_bank_len / 2 - 1) : 0;
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
    data32 = (((src_h - 1) & 0x1fff) | ((src_w - 1) & 0x1fff) << 16);
    WRITE32_REG(VPU, VPU_VPP_OSD_SCI_WH_M1, data32);
    data32 = (((display_width_ - 1) & 0xfff));
    WRITE32_REG(VPU, VPU_VPP_OSD_SCO_H_START_END, data32);
    data32 = (((display_height_ - 1) & 0xfff));
    WRITE32_REG(VPU, VPU_VPP_OSD_SCO_V_START_END, data32);
  }
  data32 = 0x0;
  if (enable) {
    data32 |=
        (vf_bank_len & 0x7) | ((vsc_ini_rcv_num & 0xf) << 3) | ((vsc_ini_rpt_p0_num & 0x3) << 8);
    data32 |= 1 << 24;
  }
  WRITE32_REG(VPU, VPU_VPP_OSD_VSC_CTRL0, data32);
  data32 = 0x0;
  if (enable) {
    data32 |=
        (hf_bank_len & 0x7) | ((hsc_ini_rcv_num & 0xf) << 3) | ((hsc_ini_rpt_p0_num & 0x3) << 8);
    data32 |= 1 << 22;
  }
  WRITE32_REG(VPU, VPU_VPP_OSD_HSC_CTRL0, data32);
  data32 = 0x0;
  if (enable) {
    data32 |= (bot_ini_phase & 0xffff) << 16;
    SET_BIT32(VPU, VPU_VPP_OSD_HSC_PHASE_STEP, hf_phase_step, 0, 28);
    SET_BIT32(VPU, VPU_VPP_OSD_HSC_INI_PHASE, 0, 0, 16);
    SET_BIT32(VPU, VPU_VPP_OSD_VSC_PHASE_STEP, vf_phase_step, 0, 28);
    WRITE32_REG(VPU, VPU_VPP_OSD_VSC_INI_PHASE, data32);
  }
}

void Osd::ResetRdmaTable() {
  // For Amlogic display driver, RDMA table is simple.
  // Setup RDMA Table Register values
  for (auto& i : rdma_chnl_container_) {
    auto* rdma_table = reinterpret_cast<RdmaTable*>(i.virt_offset);
    rdma_table[IDX_CFG_W0].reg = (VPU_VIU_OSD1_BLK0_CFG_W0 >> 2);
    rdma_table[IDX_CTRL_STAT].reg = (VPU_VIU_OSD1_CTRL_STAT >> 2);
    rdma_table[IDX_CTRL_STAT2].reg = (VPU_VIU_OSD1_CTRL_STAT2 >> 2);
    rdma_table[IDX_MATRIX_EN_CTRL].reg = (VPU_VPP_POST_MATRIX_EN_CTRL >> 2);
    rdma_table[IDX_MATRIX_COEF00_01].reg = (VPU_VPP_POST_MATRIX_COEF00_01 >> 2);
    rdma_table[IDX_MATRIX_COEF02_10].reg = (VPU_VPP_POST_MATRIX_COEF02_10 >> 2);
    rdma_table[IDX_MATRIX_COEF11_12].reg = (VPU_VPP_POST_MATRIX_COEF11_12 >> 2);
    rdma_table[IDX_MATRIX_COEF20_21].reg = (VPU_VPP_POST_MATRIX_COEF20_21 >> 2);
    rdma_table[IDX_MATRIX_COEF22].reg = (VPU_VPP_POST_MATRIX_COEF22 >> 2);
    rdma_table[IDX_MATRIX_OFFSET0_1].reg = (VPU_VPP_POST_MATRIX_OFFSET0_1 >> 2);
    rdma_table[IDX_MATRIX_OFFSET2].reg = (VPU_VPP_POST_MATRIX_OFFSET2 >> 2);
    rdma_table[IDX_MATRIX_PRE_OFFSET0_1].reg = (VPU_VPP_POST_MATRIX_PRE_OFFSET0_1 >> 2);
    rdma_table[IDX_MATRIX_PRE_OFFSET2].reg = (VPU_VPP_POST_MATRIX_PRE_OFFSET2 >> 2);
    rdma_table[IDX_GAMMA_EN].reg = (VPP_GAMMA_CNTL_PORT >> 2);
  }
}

void Osd::SetRdmaTableValue(uint32_t channel, uint32_t idx, uint32_t val) {
  ZX_DEBUG_ASSERT(idx < IDX_MAX);
  ZX_DEBUG_ASSERT(channel < kMaxRdmaChannels);
  auto* rdma_table = reinterpret_cast<RdmaTable*>(rdma_chnl_container_[channel].virt_offset);
  rdma_table[idx].val = val;
}

void Osd::FlushRdmaTable(uint32_t channel) {
  zx_status_t status =
      zx_cache_flush(rdma_chnl_container_[channel].virt_offset, IDX_MAX * sizeof(RdmaTable),
                     ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
  if (status != ZX_OK) {
    DISP_ERROR("Could not clean cache %d\n", status);
    return;
  }
}

int Osd::GetNextAvailableRdmaChannel() {
  // The next RDMA channel is the one that is not being used by hardware
  // A channel is considered available if it's not busy OR the done bit is set
  for (int i = 0; i < kMaxRdmaChannels; i++) {
    if (!rdma_chnl_container_[i].active ||
        vpu_mmio_->Read32(VPU_RDMA_STATUS) & RDMA_STATUS_DONE(i)) {
      // found one
      rdma_chnl_container_[i].active = true;
      // clear interrupts
      vpu_mmio_->Write32(vpu_mmio_->Read32(VPU_RDMA_CTRL) | RDMA_CTRL_INT_DONE(i), VPU_RDMA_CTRL);
      return i;
    }
  }

  return -1;
}

zx_status_t Osd::SetupRdma() {
  zx_status_t status = ZX_OK;
  DISP_INFO("Setting up Display RDMA\n");

  // since we are flushing the caches, make sure the tables are at least cache_line apart
  ZX_DEBUG_ASSERT(kChannelBaseOffset > zx_system_get_dcache_line_size());

  // Allocate one page for RDMA Table
  status = zx_vmo_create_contiguous(bti_.get(), ZX_PAGE_SIZE, 0, rdma_vmo_.reset_and_get_address());
  if (status != ZX_OK) {
    DISP_ERROR("Could not create RDMA VMO (%d)\n", status);
    return status;
  }

  status = zx_bti_pin(bti_.get(), ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE, rdma_vmo_.get(), 0,
                      ZX_PAGE_SIZE, &rdma_phys_, 1, &rdma_pmt_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not pin RDMA VMO (%d)\n", status);
    return status;
  }

  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, rdma_vmo_.get(),
                       0, ZX_PAGE_SIZE, reinterpret_cast<zx_vaddr_t*>(&rdma_vbuf_));
  if (status != ZX_OK) {
    DISP_ERROR("Could not map vmar (%d)\n", status);
    return status;
  }

  // Initialize each rdma channel container
  for (int i = 0; i < kMaxRdmaChannels; i++) {
    ZX_DEBUG_ASSERT((i * kChannelBaseOffset) < ZX_PAGE_SIZE);
    rdma_chnl_container_[i].phys_offset = rdma_phys_ + (i * kChannelBaseOffset);
    rdma_chnl_container_[i].virt_offset = rdma_vbuf_ + (i * kChannelBaseOffset);
    rdma_chnl_container_[i].active = false;
  }

  // Setup RDMA_CTRL:
  // Default: no reset, no clock gating, burst size 4x16B for read and write
  // DDR Read/Write request urgent
  uint32_t regVal = RDMA_CTRL_READ_URGENT | RDMA_CTRL_WRITE_URGENT;
  vpu_mmio_->Write32(regVal, VPU_RDMA_CTRL);

  ResetRdmaTable();

  return status;
}

void Osd::EnableGamma() {
  VppGammaCntlPortReg::Get().ReadFrom(&(*vpu_mmio_)).set_en(1).WriteTo(&(*vpu_mmio_));
}
void Osd::DisableGamma() {
  VppGammaCntlPortReg::Get().ReadFrom(&(*vpu_mmio_)).set_en(0).WriteTo(&(*vpu_mmio_));
}

zx_status_t Osd::WaitForGammaAddressReady() {
  // The following delay and retry count is from hardware vendor
  constexpr int32_t kGammaRetry = 100;
  constexpr zx::duration kGammaDelay = zx::usec(10);
  auto retry = kGammaRetry;
  while (!(VppGammaCntlPortReg::Get().ReadFrom(&(*vpu_mmio_)).adr_rdy()) && retry--) {
    zx::nanosleep(zx::deadline_after(kGammaDelay));
  }
  if (retry <= 0) {
    return ZX_ERR_TIMED_OUT;
  }
  return ZX_OK;
}

zx_status_t Osd::WaitForGammaWriteReady() {
  // The following delay and retry count is from hardware vendor
  constexpr int32_t kGammaRetry = 100;
  constexpr zx::duration kGammaDelay = zx::usec(10);
  auto retry = kGammaRetry;
  while (!(VppGammaCntlPortReg::Get().ReadFrom(&(*vpu_mmio_)).wr_rdy()) && retry--) {
    zx::nanosleep(zx::deadline_after(kGammaDelay));
  }
  if (retry <= 0) {
    return ZX_ERR_TIMED_OUT;
  }
  return ZX_OK;
}

zx_status_t Osd::SetGamma(GammaChannel channel, const float* data) {
  // Make sure Video Encoder is enabled
  // WRITE32_REG(VPU, ENCL_VIDEO_EN, 0);
  if (!(vpu_mmio_->Read32(ENCL_VIDEO_EN) & 0x1)) {
    return ZX_ERR_UNAVAILABLE;
  }

  // Wait for ADDR port to be ready
  zx_status_t status;
  if ((status = WaitForGammaAddressReady()) != ZX_OK) {
    return status;
  }

  // Select channel and enable auto-increment.
  // auto-increment: increments the gamma table address as we write into the
  // data register
  auto gamma_addrport_reg = VppGammaAddrPortReg::Get().FromValue(0);
  gamma_addrport_reg.set_auto_inc(1);
  gamma_addrport_reg.set_adr(0);
  switch (channel) {
    case GammaChannel::kRed:
      gamma_addrport_reg.set_sel_r(1);
      break;
    case GammaChannel::kGreen:
      gamma_addrport_reg.set_sel_g(1);
      break;
    case GammaChannel::kBlue:
      gamma_addrport_reg.set_sel_b(1);
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  gamma_addrport_reg.WriteTo(&(*vpu_mmio_));

  // Write Gamma Table
  for (size_t i = 0; i < kGammaTableSize; i++) {
    // Only write if ready. The delay seems very excessive but this comes from vendor.
    status = WaitForGammaWriteReady();
    if (status != ZX_OK) {
      return status;
    }
    auto val = std::clamp(static_cast<uint16_t>(std::round(data[i] * 1023.0)),
                          static_cast<uint16_t>(0), static_cast<uint16_t>(1023));
    VppGammaDataPortReg::Get().FromValue(0).set_reg_value(val).WriteTo(&(*vpu_mmio_));
  }

  // Wait for ADDR port to be ready
  if ((status = WaitForGammaAddressReady()) != ZX_OK) {
    return status;
  }
  return ZX_OK;
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
  regVal |= 4 << 5;   // hold_fifo_lines
  regVal |= 1 << 10;  // burst_len_sel 3 = 64. This bit is split between 10 and 31
  regVal |= 2 << 22;
  regVal |= 2 << 24;
  regVal |= 1 << 31;
  regVal |= 32 << 12;  // fifo_depth_val: 32*8 = 256
  WRITE32_REG(VPU, VPU_VIU_OSD1_FIFO_CTRL_STAT, regVal);
  WRITE32_REG(VPU, VPU_VIU_OSD2_FIFO_CTRL_STAT, regVal);

  SET_MASK32(VPU, VPP_MISC, VPP_POSTBLEND_EN);
  CLEAR_MASK32(VPU, VPP_MISC, VPP_PREBLEND_EN);
  // just disable osd to avoid booting hang up
  regVal = 0x1 << 0;
  regVal |= kMaximumAlpha << kOsdGlobalAlphaShift;
  regVal |= (1 << 21);
  WRITE32_REG(VPU, VPU_VIU_OSD1_CTRL_STAT, regVal);
  WRITE32_REG(VPU, VPU_VIU_OSD2_CTRL_STAT, regVal);

  DefaultSetup();

  EnableScaling(false);

  // Apply scale coefficients
  SET_BIT32(VPU, VPU_VPP_OSD_SCALE_COEF_IDX, 0x0000, 0, 9);
  for (unsigned int i : osd_filter_coefs_bicubic) {
    WRITE32_REG(VPU, VPU_VPP_OSD_SCALE_COEF, i);
  }

  SET_BIT32(VPU, VPU_VPP_OSD_SCALE_COEF_IDX, 0x0100, 0, 9);
  for (unsigned int i : osd_filter_coefs_bicubic) {
    WRITE32_REG(VPU, VPU_VPP_OSD_SCALE_COEF, i);
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
    reg = offset + VPU_VIU_OSD1_CTRL_STAT2;
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

  DISP_INFO("Dumping all RDMA related Registers\n\n");
  DISP_INFO("VPU_RDMA_AHB_START_ADDR_MAN = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_AHB_START_ADDR_MAN));
  DISP_INFO("VPU_RDMA_AHB_END_ADDR_MAN = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_AHB_END_ADDR_MAN));
  DISP_INFO("VPU_RDMA_AHB_START_ADDR_1 = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_AHB_START_ADDR_1));
  DISP_INFO("VPU_RDMA_AHB_END_ADDR_1 = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_AHB_END_ADDR_1));
  DISP_INFO("VPU_RDMA_AHB_START_ADDR_2 = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_AHB_START_ADDR_2));
  DISP_INFO("VPU_RDMA_AHB_END_ADDR_2 = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_AHB_END_ADDR_2));
  DISP_INFO("VPU_RDMA_AHB_START_ADDR_3 = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_AHB_START_ADDR_3));
  DISP_INFO("VPU_RDMA_AHB_END_ADDR_3 = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_AHB_END_ADDR_3));
  DISP_INFO("VPU_RDMA_AHB_START_ADDR_4 = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_AHB_START_ADDR_4));
  DISP_INFO("VPU_RDMA_AHB_END_ADDR_4 = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_AHB_END_ADDR_4));
  DISP_INFO("VPU_RDMA_AHB_START_ADDR_5 = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_AHB_START_ADDR_5));
  DISP_INFO("VPU_RDMA_AHB_END_ADDR_5 = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_AHB_END_ADDR_5));
  DISP_INFO("VPU_RDMA_AHB_START_ADDR_6 = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_AHB_START_ADDR_6));
  DISP_INFO("VPU_RDMA_AHB_END_ADDR_6 = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_AHB_END_ADDR_6));
  DISP_INFO("VPU_RDMA_AHB_START_ADDR_7 = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_AHB_START_ADDR_7));
  DISP_INFO("VPU_RDMA_AHB_END_ADDR_7 = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_AHB_END_ADDR_7));
  DISP_INFO("VPU_RDMA_ACCESS_AUTO = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_ACCESS_AUTO));
  DISP_INFO("VPU_RDMA_ACCESS_AUTO2 = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_ACCESS_AUTO2));
  DISP_INFO("VPU_RDMA_ACCESS_AUTO3 = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_ACCESS_AUTO3));
  DISP_INFO("VPU_RDMA_ACCESS_MAN = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_ACCESS_MAN));
  DISP_INFO("VPU_RDMA_CTRL = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_CTRL));
  DISP_INFO("VPU_RDMA_STATUS = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_STATUS));
  DISP_INFO("VPU_RDMA_STATUS2 = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_STATUS2));
  DISP_INFO("VPU_RDMA_STATUS3 = 0x%x\n", vpu_mmio_->Read32(VPU_RDMA_STATUS3));

  DISP_INFO("Dumping all Color Correction Matrix related Registers\n\n");
  DISP_INFO("VPU_VPP_POST_MATRIX_COEF00_01 = 0x%x\n",
            vpu_mmio_->Read32(VPU_VPP_POST_MATRIX_COEF00_01));
  DISP_INFO("VPU_VPP_POST_MATRIX_COEF02_10 = 0x%x\n",
            vpu_mmio_->Read32(VPU_VPP_POST_MATRIX_COEF02_10));
  DISP_INFO("VPU_VPP_POST_MATRIX_COEF11_12 = 0x%x\n",
            vpu_mmio_->Read32(VPU_VPP_POST_MATRIX_COEF11_12));
  DISP_INFO("VPU_VPP_POST_MATRIX_COEF20_21 = 0x%x\n",
            vpu_mmio_->Read32(VPU_VPP_POST_MATRIX_COEF20_21));
  DISP_INFO("VPU_VPP_POST_MATRIX_COEF22 = 0x%x\n", vpu_mmio_->Read32(VPU_VPP_POST_MATRIX_COEF22));
  DISP_INFO("VPU_VPP_POST_MATRIX_OFFSET0_1 = 0x%x\n",
            vpu_mmio_->Read32(VPU_VPP_POST_MATRIX_OFFSET0_1));
  DISP_INFO("VPU_VPP_POST_MATRIX_OFFSET2 = 0x%x\n", vpu_mmio_->Read32(VPU_VPP_POST_MATRIX_OFFSET2));
  DISP_INFO("VPU_VPP_POST_MATRIX_PRE_OFFSET0_1 = 0x%x\n",
            vpu_mmio_->Read32(VPU_VPP_POST_MATRIX_PRE_OFFSET0_1));
  DISP_INFO("VPU_VPP_POST_MATRIX_PRE_OFFSET2 = 0x%x\n",
            vpu_mmio_->Read32(VPU_VPP_POST_MATRIX_PRE_OFFSET2));
  DISP_INFO("VPU_VPP_POST_MATRIX_EN_CTRL = 0x%x\n", vpu_mmio_->Read32(VPU_VPP_POST_MATRIX_EN_CTRL));
}

void Osd::Release() {
  Disable();
  rdma_irq_.destroy();
  thrd_join(rdma_thread_, NULL);
  zx_pmt_unpin(rdma_pmt_);
}

}  // namespace amlogic_display
