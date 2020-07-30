// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_TESTING_IMAGE_H_
#define SRC_GRAPHICS_DISPLAY_TESTING_IMAGE_H_

#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

// Indicies into event and event_ids
#define WAIT_EVENT 0
#define SIGNAL_EVENT 1

namespace testing {
namespace display {

typedef struct image_import {
  uint64_t id;
  zx::event events[2];
  uint64_t event_ids[2];
} image_import_t;

class Image {
 public:
  static Image* Create(::llcpp::fuchsia::hardware::display::Controller::SyncClient* dc,
                       uint32_t width, uint32_t height, zx_pixel_format_t format, uint32_t fg_color,
                       uint32_t bg_color, uint64_t modifier);

  void Render(int32_t prev_step, int32_t step_num);

  void* buffer() { return buf_; }
  uint32_t width() { return width_; }
  uint32_t height() { return height_; }
  uint32_t stride() { return stride_; }
  zx_pixel_format_t format() { return format_; }

  void GetConfig(::llcpp::fuchsia::hardware::display::ImageConfig* config_out);
  bool Import(::llcpp::fuchsia::hardware::display::Controller::SyncClient* dc,
              image_import_t* import_out);

 private:
  Image(uint32_t width, uint32_t height, int32_t stride, zx_pixel_format_t format,
        uint32_t collection_id, void* buf, uint32_t fg_color, uint32_t bg_color, uint64_t modifier);

  uint32_t width_;
  uint32_t height_;
  uint32_t stride_;
  zx_pixel_format_t format_;

  uint32_t collection_id_;
  void* buf_;

  uint32_t fg_color_;
  uint32_t bg_color_;
  uint64_t modifier_;
};

}  // namespace display
}  // namespace testing

#endif  // SRC_GRAPHICS_DISPLAY_TESTING_IMAGE_H_
