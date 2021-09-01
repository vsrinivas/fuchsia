// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_TESTING_DISPLAY_H_
#define SRC_GRAPHICS_DISPLAY_TESTING_DISPLAY_H_

#include <fidl/fuchsia.hardware.display/cpp/wire.h>
#include <lib/fidl/txn_header.h>
#include <zircon/pixelformat.h>

#include <cmath>

#include <fbl/string.h>
#include <fbl/vector.h>

namespace testing {
namespace display {

struct ColorCorrectionArgs {
  ::fidl::Array<float, 3> preoffsets = {nanf("pre"), 0.0, 0.0};
  ::fidl::Array<float, 3> postoffsets = {nanf("post"), 0.0, 0.0};
  ::fidl::Array<float, 9> coeff = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};

class Display {
 public:
  Display(const fuchsia_hardware_display::wire::Info& info);

  void Init(fidl::WireSyncClient<fuchsia_hardware_display::Controller>* dc);
  void Init(fidl::WireSyncClient<fuchsia_hardware_display::Controller>* dc,
            ColorCorrectionArgs color_correction_args = ColorCorrectionArgs());

  zx_pixel_format_t format() const { return pixel_formats_[format_idx_]; }
  fuchsia_hardware_display::wire::Mode mode() const { return modes_[mode_idx_]; }
  fuchsia_hardware_display::wire::CursorInfo cursor() const { return cursors_[0]; }
  uint64_t id() const { return id_; }

  bool set_format_idx(uint32_t idx) {
    format_idx_ = idx;
    return format_idx_ < pixel_formats_.size();
  }

  bool set_mode_idx(uint32_t idx) {
    mode_idx_ = idx;
    return mode_idx_ < modes_.size();
  }

  void set_grayscale(bool grayscale) { apply_color_correction_ = grayscale_ = grayscale; }
  void apply_color_correction(bool apply) { apply_color_correction_ = apply; }

  void Dump();

 private:
  uint32_t format_idx_ = 0;
  uint32_t mode_idx_ = 0;
  bool apply_color_correction_ = false;
  bool grayscale_ = false;

  uint64_t id_;
  fbl::Vector<zx_pixel_format_t> pixel_formats_;
  fbl::Vector<fuchsia_hardware_display::wire::Mode> modes_;
  fbl::Vector<fuchsia_hardware_display::wire::CursorInfo> cursors_;

  fbl::String manufacturer_name_;
  fbl::String monitor_name_;
  fbl::String monitor_serial_;

  // Display physical dimension in millimiters
  uint32_t horizontal_size_mm_;
  uint32_t vertical_size_mm_;
  // flag used to indicate whether the values are actual values or fallback
  bool using_fallback_sizes_;
};

}  // namespace display
}  // namespace testing

#endif  // SRC_GRAPHICS_DISPLAY_TESTING_DISPLAY_H_
