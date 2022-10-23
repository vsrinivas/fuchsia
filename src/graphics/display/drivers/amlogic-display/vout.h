// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_VOUT_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_VOUT_H_

#include <fuchsia/hardware/display/controller/cpp/banjo.h>
#include <fuchsia/hardware/dsiimpl/cpp/banjo.h>
#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <lib/device-protocol/display-panel.h>
#include <lib/zx/result.h>
#include <zircon/pixelformat.h>

#include "clock.h"
#include "dsi-host.h"
#include "hdmi-host.h"

namespace amlogic_display {

enum VoutType { kDsi, kHdmi, kUnknown };

class Vout {
 public:
  Vout() = default;
  zx_status_t InitDsi(zx_device_t* parent, uint32_t panel_type, uint32_t width, uint32_t height);
  zx_status_t InitHdmi(zx_device_t* parent);

  zx_status_t RestartDisplay();

  void PopulateAddedDisplayArgs(added_display_args_t* args, uint64_t display_id);
  bool IsFormatSupported(zx_pixel_format_t format);

  VoutType type() { return type_; }
  bool supports_afbc() const { return supports_afbc_; }
  bool supports_capture() const { return supports_capture_; }
  bool supports_hpd() const { return supports_hpd_; }
  uint32_t display_width() {
    switch (type_) {
      case kDsi:
        return dsi_.disp_setting.h_active;
      case kHdmi:
        return hdmi_.cur_display_mode_.h_addressable;
      default:
        return 0;
    }
  }
  uint32_t display_height() {
    switch (type_) {
      case kDsi:
        return dsi_.disp_setting.v_active;
      case kHdmi:
        return hdmi_.cur_display_mode_.v_addressable;
      default:
        return 0;
    }
  }
  uint32_t fb_width() {
    switch (type_) {
      case kDsi:
        return dsi_.width;
      case kHdmi:
        return hdmi_.cur_display_mode_.h_addressable;
      default:
        return 0;
    }
  }
  uint32_t fb_height() {
    switch (type_) {
      case kDsi:
        return dsi_.height;
      case kHdmi:
        return hdmi_.cur_display_mode_.v_addressable;
      default:
        return 0;
    }
  }

  uint32_t panel_type() {
    switch (type_) {
      case kDsi:
        return dsi_.dsi_host->panel_type();
      case kHdmi:
      default:
        return 0;
    }
  }

  void DisplayConnected();
  void DisplayDisconnected();
  bool CheckMode(const display_mode_t* mode);
  zx_status_t ApplyConfiguration(const display_mode_t* mode);
  zx_status_t OnDisplaysChanged(added_display_info_t& info);

  zx_status_t EdidTransfer(uint32_t bus_id, const i2c_impl_op_t* op_list, size_t op_count);

  // Attempt to turn off all connected displays, and disable clocks. This will
  // also stop vsync interrupts. This is aligned with the interface for
  // fuchsia.hardware.display, where a disabled display does not produce OnVsync
  // events.
  //
  // This method is not guaranteed to power off all devices. Returns ZX_OK if
  // successful, ZX_ERR_UNSUPPORTED if the panel cannot be powered off. May
  // return other errors.
  zx::result<> PowerOff();

  // Turn on all connected displays and reprogram clocks. This will resume vsync
  // interrupts as well.
  //
  // This method is not guaranteed to power on all devices. Returns ZX_OK if
  // successful, ZX_ERR_UNSUPPORTED if the panel cannot be powered on. May
  // return other errors.
  zx::result<> PowerOn();

  void Dump();

 private:
  VoutType type_ = VoutType::kUnknown;

  // Features
  bool supports_afbc_ = false;
  bool supports_capture_ = false;
  bool supports_hpd_ = false;

  struct dsi_t {
    std::unique_ptr<amlogic_display::DsiHost> dsi_host;
    std::unique_ptr<amlogic_display::Clock> clock;

    // display dimensions and format
    uint32_t width = 0;
    uint32_t height = 0;

    // Display structure used by various layers of display controller
    display_setting_t disp_setting;
  } dsi_;

  struct hdmi_t {
    std::unique_ptr<amlogic_display::HdmiHost> hdmi_host;

    display_mode_t cur_display_mode_;
  } hdmi_;
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_VOUT_H_
