// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vpu.h"

#include <ddk/debug.h>
#include <ddktl/device.h>

#include "common.h"
#include "hhi-regs.h"
#include "vpp-regs.h"
#include "vpu-regs.h"
#include "zircon/errors.h"

namespace amlogic_display {

namespace {
constexpr uint32_t kFirstTimeLoadMagicNumber = 0x304e65;  // 0Ne
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

// Below co-efficients are used to convert 709L to RGB. The table is provided
// by Amlogic
//    ycbcr limit range, 709 to RGB
//    -16      1.164  0      1.793  0
//    -128     1.164 -0.213 -0.534  0
//    -128     1.164  2.115  0      0
constexpr uint32_t capture_yuv2rgb_coeff[3][3] = {
    {0x04a8, 0x0000, 0x072c}, {0x04a8, 0x1f26, 0x1ddd}, {0x04a8, 0x0876, 0x0000}};
constexpr uint32_t capture_yuv2rgb_preoffset[3] = {0xfc0, 0xe00, 0xe00};
constexpr uint32_t capture_yuv2rgb_offset[3] = {0, 0, 0};

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
  fbl::AutoLock lock(&capture_lock_);
  capture_state_ = CAPTURE_RESET;
  return ZX_OK;
}

bool Vpu::SetFirstTimeDriverLoad() {
  ZX_DEBUG_ASSERT(initialized_);
  uint32_t regVal = READ32_REG(VPU, VPP_DUMMY_DATA);
  if (regVal == kFirstTimeLoadMagicNumber) {
    // we have already been loaded once. don't set again.
    return false;
  }
  WRITE32_REG(VPU, VPP_DUMMY_DATA, kFirstTimeLoadMagicNumber);
  first_time_load_ = true;
  return true;
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
/*
 *  Power Table
 *  <vpu module>          <register>           <bit> <len>
 *  {VPU_VIU_OSD1,        HHI_VPU_MEM_PD_REG0,   0,   2},
 *  {VPU_VIU_OSD2,        HHI_VPU_MEM_PD_REG0,   2,   2},
 *  {VPU_VIU_VD1,         HHI_VPU_MEM_PD_REG0,   4,   2},
 *  {VPU_VIU_VD2,         HHI_VPU_MEM_PD_REG0,   6,   2},
 *  {VPU_VIU_CHROMA,      HHI_VPU_MEM_PD_REG0,   8,   2},
 *  {VPU_VIU_OFIFO,       HHI_VPU_MEM_PD_REG0,  10,   2},
 *  {VPU_VIU_SCALE,       HHI_VPU_MEM_PD_REG0,  12,   2},
 *  {VPU_VIU_OSD_SCALE,   HHI_VPU_MEM_PD_REG0,  14,   2},
 *  {VPU_VIU_VDIN0,       HHI_VPU_MEM_PD_REG0,  16,   2},
 *  {VPU_VIU_VDIN1,       HHI_VPU_MEM_PD_REG0,  18,   2},
 *  {VPU_VIU_SRSCL,       HHI_VPU_MEM_PD_REG0,  20,   2},
 *  {VPU_AFBC_DEC1,       HHI_VPU_MEM_PD_REG0,  22,   2},
 *  {VPU_VIU_DI_SCALE,    HHI_VPU_MEM_PD_REG0,  24,   2},
 *  {VPU_DI_PRE,          HHI_VPU_MEM_PD_REG0,  26,   2},
 *  {VPU_DI_POST,         HHI_VPU_MEM_PD_REG0,  28,   2},
 *  {VPU_SHARP,           HHI_VPU_MEM_PD_REG0,  30,   2},
 *  {VPU_VIU2_OSD1,       HHI_VPU_MEM_PD_REG1,   0,   2},
 *  {VPU_VIU2_OFIFO,      HHI_VPU_MEM_PD_REG1,   2,   2},
 *  {VPU_VKSTONE,         HHI_VPU_MEM_PD_REG1,   4,   2},
 *  {VPU_DOLBY_CORE3,     HHI_VPU_MEM_PD_REG1,   6,   2},
 *  {VPU_DOLBY0,          HHI_VPU_MEM_PD_REG1,   8,   2},
 *  {VPU_DOLBY1A,         HHI_VPU_MEM_PD_REG1,  10,   2},
 *  {VPU_DOLBY1B,         HHI_VPU_MEM_PD_REG1,  12,   2},
 *  {VPU_VPU_ARB,         HHI_VPU_MEM_PD_REG1,  14,   2},
 *  {VPU_AFBC_DEC,        HHI_VPU_MEM_PD_REG1,  16,   2},
 *  {VPU_VD2_SCALE,       HHI_VPU_MEM_PD_REG1,  18,   2},
 *  {VPU_VENCP,           HHI_VPU_MEM_PD_REG1,  20,   2},
 *  {VPU_VENCL,           HHI_VPU_MEM_PD_REG1,  22,   2},
 *  {VPU_VENCI,           HHI_VPU_MEM_PD_REG1,  24,   2},
 *  {VPU_VD2_OSD2_SCALE,  HHI_VPU_MEM_PD_REG1,  30,   2},
 *  {VPU_VIU_WM,          HHI_VPU_MEM_PD_REG2,   0,   2},
 *  {VPU_VIU_OSD3,        HHI_VPU_MEM_PD_REG2,   4,   2},
 *  {VPU_VIU_OSD4,        HHI_VPU_MEM_PD_REG2,   6,   2},
 *  {VPU_MAIL_AFBCD,      HHI_VPU_MEM_PD_REG2,   8,   2},
 *  {VPU_VD1_SCALE,       HHI_VPU_MEM_PD_REG2,  10,   2},
 *  {VPU_OSD_BLD34,       HHI_VPU_MEM_PD_REG2,  12,   2},
 *  {VPU_PRIME_DOLBY_RAM, HHI_VPU_MEM_PD_REG2,  14,   2},
 *  {VPU_VD2_OFIFO,       HHI_VPU_MEM_PD_REG2,  16,   2},
 *  {VPU_RDMA,            HHI_VPU_MEM_PD_REG2,  30,   2},
 */
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

void Vpu::AfbcPower(bool power_on) {
  ZX_DEBUG_ASSERT(initialized_);
  SET_BIT32(HHI, HHI_VPU_MEM_PD_REG2, power_on ? 0 : 3, 8, 2);
  zx_nanosleep(zx_deadline_after(ZX_USEC(5)));
}

zx_status_t Vpu::CaptureInit(uint8_t canvas_idx, uint32_t height, uint32_t stride) {
  ZX_DEBUG_ASSERT(initialized_);
  fbl::AutoLock lock(&capture_lock_);
  if (capture_state_ == CAPTURE_ACTIVE) {
    DISP_ERROR("Capture in progress\n");
    return ZX_ERR_UNAVAILABLE;
  }

  // setup VPU path
  VdInIfMuxCtrlReg::Get()
      .ReadFrom(&(*vpu_mmio_))
      .set_vpu_path_1(8)
      .set_vpu_path_0(8)
      .WriteTo(&(*vpu_mmio_));
  WrBackMiscCtrlReg::Get().ReadFrom(&(*vpu_mmio_)).set_chan0_hsync_enable(1).WriteTo(&(*vpu_mmio_));
  WrBackCtrlReg::Get().ReadFrom(&(*vpu_mmio_)).set_chan0_sel(5).WriteTo(&(*vpu_mmio_));

  // setup hold lines and vdin selection to internal loopback
  VdInComCtrl0Reg::Get()
      .ReadFrom(&(*vpu_mmio_))
      .set_hold_lines(0)
      .set_vdin_selection(7)
      .WriteTo(&(*vpu_mmio_));
  VdinLFifoCtrlReg::Get().FromValue(0).set_fifo_buf_size(0x780).WriteTo(&(*vpu_mmio_));

  // Setup Async Fifo
  VdInAFifoCtrl3Reg::Get()
      .ReadFrom(&(*vpu_mmio_))
      .set_data_valid_en(1)
      .set_go_field_en(1)
      .set_go_line_en(1)
      .set_vsync_pol_set(0)
      .set_hsync_pol_set(0)
      .set_vsync_sync_reset_en(1)
      .set_fifo_overflow_clr(0)
      .set_soft_reset_en(0)
      .WriteTo(&(*vpu_mmio_));

  VdInMatrixCtrlReg::Get()
      .ReadFrom(&(*vpu_mmio_))
      .set_select(1)
      .set_enable(1)
      .WriteTo(&(*vpu_mmio_));

  VdinCoef00_01Reg::Get()
      .ReadFrom(&(*vpu_mmio_))
      .set_coef00(capture_yuv2rgb_coeff[0][0])
      .set_coef01(capture_yuv2rgb_coeff[0][1])
      .WriteTo(&(*vpu_mmio_));

  VdinCoef02_10Reg::Get()
      .ReadFrom(&(*vpu_mmio_))
      .set_coef02(capture_yuv2rgb_coeff[0][2])
      .set_coef10(capture_yuv2rgb_coeff[1][0])
      .WriteTo(&(*vpu_mmio_));

  VdinCoef11_12Reg::Get()
      .ReadFrom(&(*vpu_mmio_))
      .set_coef11(capture_yuv2rgb_coeff[1][1])
      .set_coef12(capture_yuv2rgb_coeff[1][2])
      .WriteTo(&(*vpu_mmio_));

  VdinCoef20_21Reg::Get()
      .ReadFrom(&(*vpu_mmio_))
      .set_coef20(capture_yuv2rgb_coeff[2][0])
      .set_coef21(capture_yuv2rgb_coeff[2][1])
      .WriteTo(&(*vpu_mmio_));

  VdinCoef22Reg::Get()
      .ReadFrom(&(*vpu_mmio_))
      .set_coef22(capture_yuv2rgb_coeff[2][2])
      .WriteTo(&(*vpu_mmio_));

  VdinOffset0_1Reg::Get()
      .ReadFrom(&(*vpu_mmio_))
      .set_offset0(capture_yuv2rgb_offset[0])
      .set_offset1(capture_yuv2rgb_offset[1])
      .WriteTo(&(*vpu_mmio_));

  VdinOffset2Reg::Get()
      .ReadFrom(&(*vpu_mmio_))
      .set_offset2(capture_yuv2rgb_offset[2])
      .WriteTo(&(*vpu_mmio_));

  VdinPreOffset0_1Reg::Get()
      .ReadFrom(&(*vpu_mmio_))
      .set_preoffset0(capture_yuv2rgb_preoffset[0])
      .set_preoffset1(capture_yuv2rgb_preoffset[1])
      .WriteTo(&(*vpu_mmio_));

  VdinPreOffset2Reg::Get()
      .ReadFrom(&(*vpu_mmio_))
      .set_preoffset2(capture_yuv2rgb_preoffset[2])
      .WriteTo(&(*vpu_mmio_));

  // setup vdin input dimensions
  VdinIntfWidthM1Reg::Get().FromValue(stride - 1).WriteTo(&(*vpu_mmio_));

  // Configure memory size
  VdInWrHStartEndReg::Get()
      .ReadFrom(&(*vpu_mmio_))
      .set_start(0)
      .set_end(stride - 1)
      .WriteTo(&(*vpu_mmio_));
  VdInWrVStartEndReg::Get()
      .ReadFrom(&(*vpu_mmio_))
      .set_start(0)
      .set_end(height - 1)
      .WriteTo(&(*vpu_mmio_));

  // Write output canvas index, 128 bit endian, eol with width, enable 4:4:4 RGB888 mode
  VdInWrCtrlReg::Get()
      .ReadFrom(&(*vpu_mmio_))
      .set_eol_sel(1)
      .set_word_swap(1)
      .set_memory_format(1)
      .set_canvas_idx(canvas_idx)
      .WriteTo(&(*vpu_mmio_));

  // enable vdin memory power
  SET_BIT32(HHI, HHI_VPU_MEM_PD_REG0, 0, 18, 2);

  // Capture state is now in IDLE mode
  capture_state_ = CAPTURE_IDLE;
  return ZX_OK;
}

zx_status_t Vpu::CaptureStart() {
  ZX_DEBUG_ASSERT(initialized_);
  fbl::AutoLock lock(&capture_lock_);
  if (capture_state_ != CAPTURE_IDLE) {
    DISP_ERROR("Capture state is not idle! (%d)\n", capture_state_);
    return ZX_ERR_BAD_STATE;
  }

  // Now that loopback mode is configured, start capture
  // pause write output
  VdInWrCtrlReg::Get().ReadFrom(&(*vpu_mmio_)).set_write_ctrl(0).WriteTo(&(*vpu_mmio_));

  // disable vdin path
  VdInComCtrl0Reg::Get().ReadFrom(&(*vpu_mmio_)).set_enable_vdin(0).WriteTo(&(*vpu_mmio_));

  // reset mif
  VdInMiscCtrlReg::Get().ReadFrom(&(*vpu_mmio_)).set_mif_reset(1).WriteTo(&(*vpu_mmio_));
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
  VdInMiscCtrlReg::Get().ReadFrom(&(*vpu_mmio_)).set_mif_reset(0).WriteTo(&(*vpu_mmio_));

  // resume write output
  VdInWrCtrlReg::Get().ReadFrom(&(*vpu_mmio_)).set_write_ctrl(1).WriteTo(&(*vpu_mmio_));

  // wait until resets finishes
  zx_nanosleep(zx_deadline_after(ZX_MSEC(20)));

  // Clear status bit
  VdInWrCtrlReg::Get().ReadFrom(&(*vpu_mmio_)).set_done_status_clear_bit(1).WriteTo(&(*vpu_mmio_));

  // Set as urgent
  VdInWrCtrlReg::Get().ReadFrom(&(*vpu_mmio_)).set_write_req_urgent(1).WriteTo(&(*vpu_mmio_));

  // Enable loopback
  VdInWrCtrlReg::Get().ReadFrom(&(*vpu_mmio_)).set_write_mem_enable(1).WriteTo(&(*vpu_mmio_));

  // enable vdin path
  VdInComCtrl0Reg::Get().ReadFrom(&(*vpu_mmio_)).set_enable_vdin(1).WriteTo(&(*vpu_mmio_));

  capture_state_ = CAPTURE_ACTIVE;
  return ZX_OK;
}

zx_status_t Vpu::CaptureDone() {
  fbl::AutoLock lock(&capture_lock_);
  capture_state_ = CAPTURE_IDLE;
  // pause write output
  VdInWrCtrlReg::Get().ReadFrom(&(*vpu_mmio_)).set_write_ctrl(0).WriteTo(&(*vpu_mmio_));

  // disable vdin path
  VdInComCtrl0Reg::Get().ReadFrom(&(*vpu_mmio_)).set_enable_vdin(0).WriteTo(&(*vpu_mmio_));

  // reset mif
  VdInMiscCtrlReg::Get().ReadFrom(&(*vpu_mmio_)).set_mif_reset(1).WriteTo(&(*vpu_mmio_));
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
  VdInMiscCtrlReg::Get().ReadFrom(&(*vpu_mmio_)).set_mif_reset(0).WriteTo(&(*vpu_mmio_));

  return ZX_OK;
}

void Vpu::CapturePrintRegisters() {
  DISP_INFO("** Display Loopback Register Dump **\n\n");
  DISP_INFO("VdInComCtrl0Reg = 0x%x\n", VdInComCtrl0Reg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());
  DISP_INFO("VdInComStatus0Reg = 0x%x\n",
            VdInComStatus0Reg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());
  DISP_INFO("VdInMatrixCtrlReg = 0x%x\n",
            VdInMatrixCtrlReg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());
  DISP_INFO("VdinCoef00_01Reg = 0x%x\n",
            VdinCoef00_01Reg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());
  DISP_INFO("VdinCoef02_10Reg = 0x%x\n",
            VdinCoef02_10Reg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());
  DISP_INFO("VdinCoef11_12Reg = 0x%x\n",
            VdinCoef11_12Reg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());
  DISP_INFO("VdinCoef20_21Reg = 0x%x\n",
            VdinCoef20_21Reg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());
  DISP_INFO("VdinCoef22Reg = 0x%x\n", VdinCoef22Reg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());
  DISP_INFO("VdinOffset0_1Reg = 0x%x\n",
            VdinOffset0_1Reg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());
  DISP_INFO("VdinOffset2Reg = 0x%x\n", VdinOffset2Reg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());
  DISP_INFO("VdinPreOffset0_1Reg = 0x%x\n",
            VdinPreOffset0_1Reg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());
  DISP_INFO("VdinPreOffset2Reg = 0x%x\n",
            VdinPreOffset2Reg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());
  DISP_INFO("VdinLFifoCtrlReg = 0x%x\n",
            VdinLFifoCtrlReg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());
  DISP_INFO("VdinIntfWidthM1Reg = 0x%x\n",
            VdinIntfWidthM1Reg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());
  DISP_INFO("VdInWrCtrlReg = 0x%x\n", VdInWrCtrlReg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());
  DISP_INFO("VdInWrHStartEndReg = 0x%x\n",
            VdInWrHStartEndReg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());
  DISP_INFO("VdInWrVStartEndReg = 0x%x\n",
            VdInWrVStartEndReg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());
  DISP_INFO("VdInAFifoCtrl3Reg = 0x%x\n",
            VdInAFifoCtrl3Reg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());
  DISP_INFO("VdInMiscCtrlReg = 0x%x\n", VdInMiscCtrlReg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());
  DISP_INFO("VdInIfMuxCtrlReg = 0x%x\n",
            VdInIfMuxCtrlReg::Get().ReadFrom(&(*vpu_mmio_)).reg_value());

  DISP_INFO("Dumping from 0x1300 to 0x1373\n");
  for (int i = 0x1300; i <= 0x1373; i++) {
    DISP_INFO("reg[0x%x] = 0x%x\n", i, READ32_VPU_REG(i << 2));
  }
}

}  // namespace amlogic_display
