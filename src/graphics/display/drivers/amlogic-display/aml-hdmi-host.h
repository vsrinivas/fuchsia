// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AML_HDMI_HOST_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AML_HDMI_HOST_H_

#include <lib/device-protocol/pdev.h>

#include "aml-hdmitx.h"

namespace amlogic_display {

#define HDMI_COLOR_DEPTH_24B 4
#define HDMI_COLOR_DEPTH_30B 5
#define HDMI_COLOR_DEPTH_36B 6
#define HDMI_COLOR_DEPTH_48B 7

#define HDMI_COLOR_FORMAT_RGB 0
#define HDMI_COLOR_FORMAT_444 1

// AmlHdmiHost has access to the amlogic/designware HDMI block and controls its operation. It also
// handles functions and keeps track of data that the amlogic/designware block does not need to know
// about, including clock calculations (which may move out of the host after fxb/69072 is resolved),
// VPU and HHI register handling, HDMI parameters, etc.
class AmlHdmiHost {
 public:
  explicit AmlHdmiHost(zx_device_t* parent) : pdev_(ddk::PDev::FromFragment(parent)) {}

  zx_status_t Init();
  zx_status_t HostOn() { return hdmitx_->InitHw(); }
  void HostOff() { return hdmitx_->ShutDown(); }
  zx_status_t ModeSet() { return hdmitx_->InitInterface(&p_, &color_); }

  void UpdateOutputColorFormat(uint8_t output_color_format) {
    color_.output_color_format = output_color_format;
  }

  zx_status_t GetVic(const display_mode_t* disp_timing);
  zx_status_t I2cImplTransact(uint32_t bus_id, const i2c_impl_op_t* op_list, size_t op_count) {
    return hdmitx_->I2cImplTransact(bus_id, op_list, op_count);
  }

  zx_status_t ConfigurePll();

 private:
  void ConfigureHpllClkOut(uint32_t hpll);
  void ConfigureOd3Div(uint32_t div_sel);
  void WaitForPllLocked();

  ddk::PDev pdev_;

  std::unique_ptr<AmlHdmitx> hdmitx_;

  std::optional<ddk::MmioBuffer> vpu_mmio_;
  std::optional<ddk::MmioBuffer> hhi_mmio_;

  hdmi_param p_;
  hdmi_color_param color_{
      .input_color_format = HDMI_COLOR_FORMAT_444,
      .output_color_format = HDMI_COLOR_FORMAT_444,
      .color_depth = HDMI_COLOR_DEPTH_24B,
  };
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_AML_HDMI_HOST_H_
