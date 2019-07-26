// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/string.h>
#include <fbl/vector.h>
#include <zircon/pixelformat.h>

#include "fuchsia/hardware/display/c/fidl.h"

class Display {
 public:
  Display(fuchsia_hardware_display_Info* info);

  void Init(zx_handle_t dc_handle);

  zx_pixel_format_t format() const { return pixel_formats_[format_idx_]; }
  fuchsia_hardware_display_Mode mode() const { return modes_[mode_idx_]; }
  fuchsia_hardware_display_CursorInfo cursor() const { return cursors_[0]; }
  uint64_t id() const { return id_; }

  bool set_format_idx(uint32_t idx) {
    format_idx_ = idx;
    return format_idx_ < pixel_formats_.size();
  }

  bool set_mode_idx(uint32_t idx) {
    mode_idx_ = idx;
    return mode_idx_ < modes_.size();
  }

  void set_grayscale(bool grayscale) { grayscale_ = grayscale; }

  void Dump();

 private:
  uint32_t format_idx_ = 0;
  uint32_t mode_idx_ = 0;
  bool grayscale_ = false;

  uint64_t id_;
  fbl::Vector<zx_pixel_format_t> pixel_formats_;
  fbl::Vector<fuchsia_hardware_display_Mode> modes_;
  fbl::Vector<fuchsia_hardware_display_CursorInfo> cursors_;

  fbl::String manufacturer_name_;
  fbl::String monitor_name_;
  fbl::String monitor_serial_;
};
