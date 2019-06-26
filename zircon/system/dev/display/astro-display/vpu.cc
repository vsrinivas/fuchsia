// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vpu.h"

#include <ddk/debug.h>
#include <ddktl/device.h>

#include "hhi-regs.h"
#include "vpp-regs.h"
#include "vpu-regs.h"

namespace astro_display {

namespace {
constexpr uint32_t kVpuMux = 0;
constexpr uint32_t kVpuDiv = 3;

constexpr int16_t RGB709_to_YUV709l_coeff[24] = {
    0x0000, 0x0000, 0x0000, 0x00bb, 0x0275, 0x003f, 0x1f99, 0x1ea6, 0x01c2, 0x01c2, 0x1e67, 0x1fd7,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0040, 0x0200, 0x0200, 0x0000, 0x0000, 0x0000,
};

constexpr int16_t YUV709l_to_RGB709_coeff12[24] = {
    -256, -2048, -2048, 4788, 0, 7372, 4788, -876, -2190, 4788, 8686, 0,
    0,    0,     0,     0,    0, 0,    0,    0,    0,     0,    0,    0,
};
}  // namespace

// AOBUS Register
#define AOBUS_GEN_PWR_SLEEP0 (0x03a << 2)

// CBUS Reset Register
#define RESET0_LEVEL (0x0420 << 2)
#define RESET1_LEVEL (0x0421 << 2)
#define RESET2_LEVEL (0x0422 << 2)
#define RESET4_LEVEL (0x0424 << 2)
#define RESET7_LEVEL (0x0427 << 2)

#define READ32_VPU_REG(a) vpu_mmio_->Read32(a)
#define WRITE32_VPU_REG(a, v) vpu_mmio_->Write32(v, a)

#define READ32_HHI_REG(a) hhi_mmio_->Read32(a)
#define WRITE32_HHI_REG(a, v) hhi_mmio_->Write32(v, a)

#define READ32_AOBUS_REG(a) aobus_mmio_->Read32(a)
#define WRITE32_AOBUS_REG(a, v) aobus_mmio_->Write32(v, a)

#define READ32_CBUS_REG(a) cbus_mmio_->Read32(a)
#define WRITE32_CBUS_REG(a, v) cbus_mmio_->Write32(v, a)

zx_status_t Vpu::Init(zx_device_t* parent) {
  if (initialized_) {
    return ZX_OK;
  }
  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev_);
  if (status != ZX_OK) {
    return status;
  }

  // Map VPU registers
  mmio_buffer_t mmio;
  status = pdev_map_mmio_buffer(&pdev_, MMIO_VPU, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("vpu: Could not map VPU mmio\n");
    return status;
  }
  vpu_mmio_ = ddk::MmioBuffer(mmio);

  // Map HHI registers
  status = pdev_map_mmio_buffer(&pdev_, MMIO_HHI, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("vpu: Could not map HHI mmio\n");
    return status;
  }
  hhi_mmio_ = ddk::MmioBuffer(mmio);

  // Map AOBUS registers
  status = pdev_map_mmio_buffer(&pdev_, MMIO_AOBUS, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("vpu: Could not map AOBUS mmio\n");
    return status;
  }
  aobus_mmio_ = ddk::MmioBuffer(mmio);

  // Map CBUS registers
  status = pdev_map_mmio_buffer(&pdev_, MMIO_CBUS, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("vpu: Could not map CBUS mmio\n");
    return status;
  }
  cbus_mmio_ = ddk::MmioBuffer(mmio);

  // VPU object is ready to be used
  initialized_ = true;
  return ZX_OK;
}

void Vpu::VppInit() {
  ZX_DEBUG_ASSERT(initialized_);

  // init vpu fifo control register
  SET_BIT32(VPU, VPP_OFIFO_SIZE, 0xFFF, 0, 12);
  WRITE32_REG(VPU, VPP_HOLD_LINES, 0x08080808);
  // default probe_sel, for highlight en
  SET_BIT32(VPU, VPP_MATRIX_CTRL, 0x7, 12, 3);

  // setting up os1 for rgb -> yuv limit
  const int16_t* m = RGB709_to_YUV709l_coeff;

  // VPP WRAP OSD1 matrix
  WRITE32_REG(VPU, VPP_WRAP_OSD1_MATRIX_PRE_OFFSET0_1, ((m[0] & 0xfff) << 16) | (m[1] & 0xfff));
  WRITE32_REG(VPU, VPP_WRAP_OSD1_MATRIX_PRE_OFFSET2, m[2] & 0xfff);
  WRITE32_REG(VPU, VPP_WRAP_OSD1_MATRIX_COEF00_01, ((m[3] & 0x1fff) << 16) | (m[4] & 0x1fff));
  WRITE32_REG(VPU, VPP_WRAP_OSD1_MATRIX_COEF02_10, ((m[5] & 0x1fff) << 16) | (m[6] & 0x1fff));
  WRITE32_REG(VPU, VPP_WRAP_OSD1_MATRIX_COEF11_12, ((m[7] & 0x1fff) << 16) | (m[8] & 0x1fff));
  WRITE32_REG(VPU, VPP_WRAP_OSD1_MATRIX_COEF20_21, ((m[9] & 0x1fff) << 16) | (m[10] & 0x1fff));
  WRITE32_REG(VPU, VPP_WRAP_OSD1_MATRIX_COEF22, m[11] & 0x1fff);
  WRITE32_REG(VPU, VPP_WRAP_OSD1_MATRIX_OFFSET0_1, ((m[18] & 0xfff) << 16) | (m[19] & 0xfff));
  WRITE32_REG(VPU, VPP_WRAP_OSD1_MATRIX_OFFSET2, m[20] & 0xfff);
  SET_BIT32(VPU, VPP_WRAP_OSD1_MATRIX_EN_CTRL, 1, 0, 1);

  // VPP WRAP OSD2 matrix
  WRITE32_REG(VPU, VPP_WRAP_OSD2_MATRIX_PRE_OFFSET0_1, ((m[0] & 0xfff) << 16) | (m[1] & 0xfff));
  WRITE32_REG(VPU, VPP_WRAP_OSD2_MATRIX_PRE_OFFSET2, m[2] & 0xfff);
  WRITE32_REG(VPU, VPP_WRAP_OSD2_MATRIX_COEF00_01, ((m[3] & 0x1fff) << 16) | (m[4] & 0x1fff));
  WRITE32_REG(VPU, VPP_WRAP_OSD2_MATRIX_COEF02_10, ((m[5] & 0x1fff) << 16) | (m[6] & 0x1fff));
  WRITE32_REG(VPU, VPP_WRAP_OSD2_MATRIX_COEF11_12, ((m[7] & 0x1fff) << 16) | (m[8] & 0x1fff));
  WRITE32_REG(VPU, VPP_WRAP_OSD2_MATRIX_COEF20_21, ((m[9] & 0x1fff) << 16) | (m[10] & 0x1fff));
  WRITE32_REG(VPU, VPP_WRAP_OSD2_MATRIX_COEF22, m[11] & 0x1fff);
  WRITE32_REG(VPU, VPP_WRAP_OSD2_MATRIX_OFFSET0_1, ((m[18] & 0xfff) << 16) | (m[19] & 0xfff));
  WRITE32_REG(VPU, VPP_WRAP_OSD2_MATRIX_OFFSET2, m[20] & 0xfff);
  SET_BIT32(VPU, VPP_WRAP_OSD2_MATRIX_EN_CTRL, 1, 0, 1);

  // VPP WRAP OSD3 matrix
  WRITE32_REG(VPU, VPP_WRAP_OSD3_MATRIX_PRE_OFFSET0_1, ((m[0] & 0xfff) << 16) | (m[1] & 0xfff));
  WRITE32_REG(VPU, VPP_WRAP_OSD3_MATRIX_PRE_OFFSET2, m[2] & 0xfff);
  WRITE32_REG(VPU, VPP_WRAP_OSD3_MATRIX_COEF00_01, ((m[3] & 0x1fff) << 16) | (m[4] & 0x1fff));
  WRITE32_REG(VPU, VPP_WRAP_OSD3_MATRIX_COEF02_10, ((m[5] & 0x1fff) << 16) | (m[6] & 0x1fff));
  WRITE32_REG(VPU, VPP_WRAP_OSD3_MATRIX_COEF11_12, ((m[7] & 0x1fff) << 16) | (m[8] & 0x1fff));
  WRITE32_REG(VPU, VPP_WRAP_OSD3_MATRIX_COEF20_21, ((m[9] & 0x1fff) << 16) | (m[10] & 0x1fff));
  WRITE32_REG(VPU, VPP_WRAP_OSD3_MATRIX_COEF22, m[11] & 0x1fff);
  WRITE32_REG(VPU, VPP_WRAP_OSD3_MATRIX_OFFSET0_1, ((m[18] & 0xfff) << 16) | (m[19] & 0xfff));
  WRITE32_REG(VPU, VPP_WRAP_OSD3_MATRIX_OFFSET2, m[20] & 0xfff);
  SET_BIT32(VPU, VPP_WRAP_OSD3_MATRIX_EN_CTRL, 1, 0, 1);

  WRITE32_REG(VPU, DOLBY_PATH_CTRL, 0xf);

  // POST2 matrix: YUV limit -> RGB  default is 12bit
  m = YUV709l_to_RGB709_coeff12;

  // VPP WRAP POST2 matrix
  WRITE32_REG(VPU, VPP_POST2_MATRIX_PRE_OFFSET0_1,
              (((m[0] >> 2) & 0xfff) << 16) | ((m[1] >> 2) & 0xfff));
  WRITE32_REG(VPU, VPP_POST2_MATRIX_PRE_OFFSET2, (m[2] >> 2) & 0xfff);
  WRITE32_REG(VPU, VPP_POST2_MATRIX_COEF00_01,
              (((m[3] >> 2) & 0x1fff) << 16) | ((m[4] >> 2) & 0x1fff));
  WRITE32_REG(VPU, VPP_POST2_MATRIX_COEF02_10,
              (((m[5] >> 2) & 0x1fff) << 16) | ((m[6] >> 2) & 0x1fff));
  WRITE32_REG(VPU, VPP_POST2_MATRIX_COEF11_12,
              (((m[7] >> 2) & 0x1fff) << 16) | ((m[8] >> 2) & 0x1fff));
  WRITE32_REG(VPU, VPP_POST2_MATRIX_COEF20_21,
              (((m[9] >> 2) & 0x1fff) << 16) | ((m[10] >> 2) & 0x1fff));
  WRITE32_REG(VPU, VPP_POST2_MATRIX_COEF22, (m[11] >> 2) & 0x1fff);
  WRITE32_REG(VPU, VPP_POST2_MATRIX_OFFSET0_1,
              (((m[18] >> 2) & 0xfff) << 16) | ((m[19] >> 2) & 0xfff));
  WRITE32_REG(VPU, VPP_POST2_MATRIX_OFFSET2, (m[20] >> 2) & 0xfff);
  SET_BIT32(VPU, VPP_POST2_MATRIX_EN_CTRL, 1, 0, 1);

  SET_BIT32(VPU, VPP_MATRIX_CTRL, 1, 0, 1);
  SET_BIT32(VPU, VPP_MATRIX_CTRL, 0, 8, 3);

  // 709L to RGB
  WRITE32_REG(VPU, VPP_MATRIX_PRE_OFFSET0_1, 0x0FC00E00);
  WRITE32_REG(VPU, VPP_MATRIX_PRE_OFFSET2, 0x00000E00);
  // ycbcr limit range, 709 to RGB
  // -16      1.164  0      1.793  0
  // -128     1.164 -0.213 -0.534  0
  // -128     1.164  2.115  0      0
  WRITE32_REG(VPU, VPP_MATRIX_COEF00_01, 0x04A80000);
  WRITE32_REG(VPU, VPP_MATRIX_COEF02_10, 0x072C04A8);
  WRITE32_REG(VPU, VPP_MATRIX_COEF11_12, 0x1F261DDD);
  WRITE32_REG(VPU, VPP_MATRIX_COEF20_21, 0x04A80876);
  WRITE32_REG(VPU, VPP_MATRIX_COEF22, 0x0);
  WRITE32_REG(VPU, VPP_MATRIX_OFFSET0_1, 0x0);
  WRITE32_REG(VPU, VPP_MATRIX_OFFSET2, 0x0);

  SET_BIT32(VPU, VPP_MATRIX_CLIP, 0, 5, 3);
}

void Vpu::ConfigureClock() {
  ZX_DEBUG_ASSERT(initialized_);
  // vpu clock
  WRITE32_REG(HHI, HHI_VPU_CLK_CNTL, ((kVpuMux << 9) | (kVpuDiv << 0)));
  SET_BIT32(HHI, HHI_VPU_CLK_CNTL, 1, 8, 1);

  // vpu clkb
  // bit 0 is set since kVpuClkFrequency > clkB max frequency (350MHz)
  WRITE32_REG(HHI, HHI_VPU_CLKB_CNTL, ((1 << 8) | (1 << 0)));

  // vapb clk
  // turn on ge2d clock since kVpuClkFrequency > 250MHz
  WRITE32_REG(HHI, HHI_VAPBCLK_CNTL, (1 << 30) | (0 << 9) | (1 << 0));

  SET_BIT32(HHI, HHI_VAPBCLK_CNTL, 1, 8, 1);

  SET_BIT32(HHI, HHI_VID_CLK_CNTL2, 0, 0, 8);

  // dmc_arb_config
  WRITE32_REG(VPU, VPU_RDARB_MODE_L1C1, 0x0);
  WRITE32_REG(VPU, VPU_RDARB_MODE_L1C2, 0x10000);
  WRITE32_REG(VPU, VPU_RDARB_MODE_L2C1, 0x900000);
  WRITE32_REG(VPU, VPU_WRARB_MODE_L2C1, 0x20000);
}

void Vpu::PowerOn() {
  ZX_DEBUG_ASSERT(initialized_);
  SET_BIT32(AOBUS, AOBUS_GEN_PWR_SLEEP0, 0, 8, 1);  // [8] power on

  // power up memories
  for (int i = 0; i < 32; i += 2) {
    SET_BIT32(HHI, HHI_VPU_MEM_PD_REG0, 0, i, 2);
    zx_nanosleep(zx_deadline_after(ZX_USEC(5)));
  }
  for (int i = 0; i < 32; i += 2) {
    SET_BIT32(HHI, HHI_VPU_MEM_PD_REG1, 0, i, 2);
    zx_nanosleep(zx_deadline_after(ZX_USEC(5)));
  }
  SET_BIT32(HHI, HHI_VPU_MEM_PD_REG2, 0, 0, 2);
  zx_nanosleep(zx_deadline_after(ZX_USEC(5)));
  for (int i = 4; i < 18; i += 2) {
    SET_BIT32(HHI, HHI_VPU_MEM_PD_REG2, 0, i, 2);
    zx_nanosleep(zx_deadline_after(ZX_USEC(5)));
  }
  SET_BIT32(HHI, HHI_VPU_MEM_PD_REG2, 0, 30, 2);
  zx_nanosleep(zx_deadline_after(ZX_USEC(5)));

  for (int i = 8; i < 16; i++) {
    SET_BIT32(HHI, HHI_MEM_PD_REG0, 0, i, 1);
    zx_nanosleep(zx_deadline_after(ZX_USEC(5)));
  }
  zx_nanosleep(zx_deadline_after(ZX_USEC(20)));

  // Reset VIU + VENC
  // Reset VENCI + VENCP + VADC + VENCL
  // Reset HDMI-APB + HDMI-SYS + HDMI-TX + HDMI-CEC
  CLEAR_MASK32(CBUS, RESET0_LEVEL, ((1 << 5) | (1 << 10) | (1 << 19) | (1 << 13)));
  CLEAR_MASK32(CBUS, RESET1_LEVEL, (1 << 5));
  CLEAR_MASK32(CBUS, RESET2_LEVEL, (1 << 15));
  CLEAR_MASK32(CBUS, RESET4_LEVEL,
               ((1 << 6) | (1 << 7) | (1 << 13) | (1 << 5) | (1 << 9) | (1 << 4) | (1 << 12)));
  CLEAR_MASK32(CBUS, RESET7_LEVEL, (1 << 7));

  // Remove VPU_HDMI ISO
  SET_BIT32(AOBUS, AOBUS_GEN_PWR_SLEEP0, 0, 9, 1);  // [9] VPU_HDMI

  // release Reset
  SET_MASK32(CBUS, RESET0_LEVEL, ((1 << 5) | (1 << 10) | (1 << 19) | (1 << 13)));
  SET_MASK32(CBUS, RESET1_LEVEL, (1 << 5));
  SET_MASK32(CBUS, RESET2_LEVEL, (1 << 15));
  SET_MASK32(CBUS, RESET4_LEVEL,
             ((1 << 6) | (1 << 7) | (1 << 13) | (1 << 5) | (1 << 9) | (1 << 4) | (1 << 12)));
  SET_MASK32(CBUS, RESET7_LEVEL, (1 << 7));

  ConfigureClock();
}

void Vpu::PowerOff() {
  ZX_DEBUG_ASSERT(initialized_);
  // Power down VPU_HDMI
  // Enable Isolation
  SET_BIT32(AOBUS, AOBUS_GEN_PWR_SLEEP0, 1, 9, 1);  // ISO
  zx_nanosleep(zx_deadline_after(ZX_USEC(20)));

  // power down memories
  for (int i = 0; i < 32; i += 2) {
    SET_BIT32(HHI, HHI_VPU_MEM_PD_REG0, 0x3, i, 2);
    zx_nanosleep(zx_deadline_after(ZX_USEC(5)));
  }
  for (int i = 0; i < 32; i += 2) {
    SET_BIT32(HHI, HHI_VPU_MEM_PD_REG1, 0x3, i, 2);
    zx_nanosleep(zx_deadline_after(ZX_USEC(5)));
  }
  SET_BIT32(HHI, HHI_VPU_MEM_PD_REG2, 0x3, 0, 2);
  zx_nanosleep(zx_deadline_after(ZX_USEC(5)));
  for (int i = 4; i < 18; i += 2) {
    SET_BIT32(HHI, HHI_VPU_MEM_PD_REG2, 0x3, i, 2);
    zx_nanosleep(zx_deadline_after(ZX_USEC(5)));
  }
  SET_BIT32(HHI, HHI_VPU_MEM_PD_REG2, 0x3, 30, 2);
  zx_nanosleep(zx_deadline_after(ZX_USEC(5)));

  for (int i = 8; i < 16; i++) {
    SET_BIT32(HHI, HHI_MEM_PD_REG0, 0x1, i, 1);
    zx_nanosleep(zx_deadline_after(ZX_USEC(5)));
  }
  zx_nanosleep(zx_deadline_after(ZX_USEC(20)));

  // Power down VPU domain
  SET_BIT32(AOBUS, AOBUS_GEN_PWR_SLEEP0, 1, 8, 1);  // PDN

  SET_BIT32(HHI, HHI_VAPBCLK_CNTL, 0, 8, 1);
  SET_BIT32(HHI, HHI_VPU_CLK_CNTL, 0, 8, 1);
}
}  // namespace astro_display
