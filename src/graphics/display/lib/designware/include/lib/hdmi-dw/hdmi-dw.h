// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_LIB_DESIGNWARE_INCLUDE_LIB_HDMI_DW_HDMI_DW_H_
#define SRC_GRAPHICS_DISPLAY_LIB_DESIGNWARE_INCLUDE_LIB_HDMI_DW_HDMI_DW_H_

#include <fuchsia/hardware/hdmi/llcpp/fidl.h>
#include <fuchsia/hardware/i2cimpl/cpp/banjo.h>
#include <lib/hdmi/base.h>

namespace hdmi_dw {

using fuchsia_hardware_hdmi::wire::DisplayMode;

struct hdmi_param_tx {
  uint16_t vic;
  uint8_t aspect_ratio;
  uint8_t colorimetry;
  bool is4K;
};

class HdmiDw {
 public:
  explicit HdmiDw(HdmiIpBase* base) : base_(base) {}

  zx_status_t InitHw();
  zx_status_t EdidTransfer(const i2c_impl_op_t* op_list, size_t op_count);

  void ConfigHdmitx(const DisplayMode& mode, const hdmi_param_tx& p);
  void SetupInterrupts();
  void Reset();
  void SetupScdc(bool is4k);
  void ResetFc();
  void SetFcScramblerCtrl(bool is4k);

 private:
  void WriteReg(uint32_t addr, uint32_t data) { base_->WriteIpReg(addr, data); }
  uint32_t ReadReg(uint32_t addr) { return base_->ReadIpReg(addr); }

  void ScdcWrite(uint8_t addr, uint8_t val);
  void ScdcRead(uint8_t addr, uint8_t* val);

  void ConfigCsc(const DisplayMode& mode);

  HdmiIpBase* base_;
};

}  // namespace hdmi_dw

#endif  // SRC_GRAPHICS_DISPLAY_LIB_DESIGNWARE_INCLUDE_LIB_HDMI_DW_HDMI_DW_H_
