// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-hdmitx.h"

#include "hdmitx-top-regs.h"

#define HDMI_ASPECT_RATIO_NONE 0
#define HDMI_ASPECT_RATIO_4x3 1
#define HDMI_ASPECT_RATIO_16x9 2

#define HDMI_COLORIMETRY_ITU601 1
#define HDMI_COLORIMETRY_ITU709 2

#define DWC_OFFSET_MASK (0x10UL << 24)

namespace amlogic_display {

using fuchsia_hardware_hdmi::wire::ColorDepth;
using fuchsia_hardware_hdmi::wire::ColorFormat;

void AmlHdmitx::WriteReg(uint32_t reg, uint32_t val) {
  // determine if we are writing to HDMI TOP (AMLOGIC Wrapper) or HDMI IP
  uint32_t offset = (reg & DWC_OFFSET_MASK) >> 24;
  reg = reg & 0xffff;

  if (offset) {
    WriteIpReg(reg, val & 0xFF);
  } else {
    fbl::AutoLock lock(&register_lock_);
    hdmitx_mmio_->Write32(val, (reg << 2) + 0x8000);
  }

#ifdef LOG_HDMITX
  DISP_INFO("%s wr[0x%x] 0x%x\n", offset ? "DWC" : "TOP", addr, data);
#endif
}

uint32_t AmlHdmitx::ReadReg(uint32_t reg) {
  // determine if we are writing to HDMI TOP (AMLOGIC Wrapper) or HDMI IP
  uint32_t ret = 0;
  uint32_t offset = (reg & DWC_OFFSET_MASK) >> 24;
  reg = reg & 0xffff;

  if (offset) {
    ret = ReadIpReg(reg);
  } else {
    fbl::AutoLock lock(&register_lock_);
    ret = hdmitx_mmio_->Read32((reg << 2) + 0x8000);
  }

  return ret;
}

zx_status_t AmlHdmitx::Init() {
  if (!pdev_.is_valid()) {
    DISP_ERROR("AmlHdmitx: Could not get ZX_PROTOCOL_PDEV protocol\n");
    return ZX_ERR_NO_RESOURCES;
  }

  // Map registers
  auto status = pdev_.MapMmio(MMIO_MPI_DSI, &hdmitx_mmio_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map HDMITX mmio\n");
    return status;
  }

  return ZX_OK;
}

zx_status_t AmlHdmitx::InitHw() {
  // TODO(fxb/69679): Add in Resets
  // reset hdmi related blocks (HIU, HDMI SYS, HDMI_TX)
  // auto reset0_result = display->reset_register_.WriteRegister32(PRESET0_REGISTER, 1 << 19, 1 <<
  // 19); if ((reset0_result.status() != ZX_OK) || reset0_result->result.is_err()) {
  //   zxlogf(ERROR, "Reset0 Write failed\n");
  // }

  /* FIXME: This will reset the entire HDMI subsystem including the HDCP engine.
   * At this point, we have no way of initializing HDCP block, so we need to
   * skip this for now.
   */
  // auto reset2_result = display->reset_register_.WriteRegister32(PRESET2_REGISTER, 1 << 15, 1 <<
  // 15); // Will mess up hdcp stuff if ((reset2_result.status() != ZX_OK) ||
  // reset2_result->result.is_err()) {
  //   zxlogf(ERROR, "Reset2 Write failed\n");
  // }

  // auto reset2_result = display->reset_register_.WriteRegister32(PRESET2_REGISTER, 1 << 2, 1 <<
  // 2); if ((reset2_result.status() != ZX_OK) || reset2_result->result.is_err()) {
  //   zxlogf(ERROR, "Reset2 Write failed\n");
  // }

  // Bring HDMI out of reset
  WriteReg(HDMITX_TOP_SW_RESET, 0);
  usleep(200);
  WriteReg(HDMITX_TOP_CLK_CNTL, 0x000000ff);

  return hdmi_dw_.InitHw();
}

void AmlHdmitx::CalculateTxParam(const DisplayMode& mode, hdmi_dw::hdmi_param_tx* p) {
  if ((mode.mode().pixel_clock_10khz * 10) > 500000) {
    p->is4K = true;
  } else {
    p->is4K = false;
  }

  if (mode.mode().h_addressable * 3 == mode.mode().v_addressable * 4) {
    p->aspect_ratio = HDMI_ASPECT_RATIO_4x3;
  } else if (mode.mode().h_addressable * 9 == mode.mode().v_addressable * 16) {
    p->aspect_ratio = HDMI_ASPECT_RATIO_16x9;
  } else {
    p->aspect_ratio = HDMI_ASPECT_RATIO_NONE;
  }

  p->colorimetry = HDMI_COLORIMETRY_ITU601;
}

zx_status_t AmlHdmitx::InitInterface(const DisplayMode& mode) {
  if (!mode.has_mode() || !mode.has_color()) {
    DISP_ERROR("Display Mode needs both StandardDisplayMode and ColorParam\n");
    return ZX_ERR_INVALID_ARGS;
  }

  hdmi_dw::hdmi_param_tx p;
  CalculateTxParam(mode, &p);

  // Output normal TMDS Data
  WriteReg(HDMITX_TOP_BIST_CNTL, 1 << 12);

  // Configure HDMI TX IP
  hdmi_dw_.ConfigHdmitx(mode, p);
  WriteReg(HDMITX_TOP_INTR_STAT_CLR, 0x0000001f);
  hdmi_dw_.SetupInterrupts();
  WriteReg(HDMITX_TOP_INTR_MASKN, 0x9f);
  hdmi_dw_.Reset();

  if (p.is4K) {
    // Setup TMDS Clocks (magic numbers)
    WriteReg(HDMITX_TOP_TMDS_CLK_PTTN_01, 0);
    WriteReg(HDMITX_TOP_TMDS_CLK_PTTN_23, 0x03ff03ff);
  } else {
    WriteReg(HDMITX_TOP_TMDS_CLK_PTTN_01, 0x001f001f);
    WriteReg(HDMITX_TOP_TMDS_CLK_PTTN_23, 0x001f001f);
  }
  hdmi_dw_.SetFcScramblerCtrl(p.is4K);

  WriteReg(HDMITX_TOP_TMDS_CLK_PTTN_CNTL, 0x1);
  usleep(2);
  WriteReg(HDMITX_TOP_TMDS_CLK_PTTN_CNTL, 0x2);

  hdmi_dw_.SetupScdc(p.is4K);
  hdmi_dw_.ResetFc();

  return ZX_OK;
}

zx_status_t AmlHdmitx::I2cImplTransact(uint32_t bus_id, const i2c_impl_op_t* op_list,
                                       size_t op_count) {
  if (!op_list) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&i2c_lock_);
  return hdmi_dw_.EdidTransfer(op_list, op_count);
}

}  // namespace amlogic_display
