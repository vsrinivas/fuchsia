// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UAPP_DISPLAY_TEST_IMAGE_H_
#define ZIRCON_SYSTEM_UAPP_DISPLAY_TEST_IMAGE_H_

#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

#define TILE_PIXEL_WIDTH 32u
#define TILE_PIXEL_HEIGHT 32u
#define TILE_BYTES_PER_PIXEL 4u
#define TILE_NUM_BYTES 4096u
#define TILE_NUM_PIXELS (TILE_NUM_BYTES / TILE_BYTES_PER_PIXEL)
#define SUBTILE_COLUMN_WIDTH 4u

// Indicies into event and event_ids
#define WAIT_EVENT 0
#define SIGNAL_EVENT 1

typedef struct image_import {
  uint64_t id;
  zx::event events[2];
  uint64_t event_ids[2];
} image_import_t;

class Image {
 public:
  static Image* Create(::llcpp::fuchsia::hardware::display::Controller::SyncClient* dc,
                       uint32_t width, uint32_t height, zx_pixel_format_t format, uint32_t fg_color,
                       uint32_t bg_color, bool use_intel_y_tiling);

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
        uint32_t collection_id, void* buf, uint32_t fg_color, uint32_t bg_color,
        bool use_intel_y_tiling);

  uint32_t width_;
  uint32_t height_;
  uint32_t stride_;
  zx_pixel_format_t format_;

  uint32_t collection_id_;
  void* buf_;

  uint32_t fg_color_;
  uint32_t bg_color_;
  bool use_intel_y_tiling_;
};

#endif  // ZIRCON_SYSTEM_UAPP_DISPLAY_TEST_IMAGE_H_
