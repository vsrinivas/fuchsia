// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UAPP_DISPLAY_TEST_DISPLAY_H_
#define ZIRCON_SYSTEM_UAPP_DISPLAY_TEST_DISPLAY_H_

#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <lib/fidl/txn_header.h>
#include <zircon/pixelformat.h>

#include <fbl/string.h>
#include <fbl/vector.h>

class Display {
 public:
  Display(const ::llcpp::fuchsia::hardware::display::Info& info);

  void Init(::llcpp::fuchsia::hardware::display::Controller::SyncClient* dc);

  zx_pixel_format_t format() const { return pixel_formats_[format_idx_]; }
  ::llcpp::fuchsia::hardware::display::Mode mode() const { return modes_[mode_idx_]; }
  ::llcpp::fuchsia::hardware::display::CursorInfo cursor() const { return cursors_[0]; }
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
  fbl::Vector<::llcpp::fuchsia::hardware::display::Mode> modes_;
  fbl::Vector<::llcpp::fuchsia::hardware::display::CursorInfo> cursors_;

  fbl::String manufacturer_name_;
  fbl::String monitor_name_;
  fbl::String monitor_serial_;
};

#endif  // ZIRCON_SYSTEM_UAPP_DISPLAY_TEST_DISPLAY_H_
