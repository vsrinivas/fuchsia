// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_DISPLAY_TEST_FIDL_CLIENT_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_DISPLAY_TEST_FIDL_CLIENT_H_

#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <lib/fidl/cpp/message.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

#include <memory>

#include "base.h"

namespace display {

class TestFidlClient {
 public:
  class Display {
   public:
    Display(const ::llcpp::fuchsia::hardware::display::Info& info);

    uint64_t id_;
    fbl::Vector<zx_pixel_format_t> pixel_formats_;
    fbl::Vector<::llcpp::fuchsia::hardware::display::Mode> modes_;
    fbl::Vector<::llcpp::fuchsia::hardware::display::CursorInfo> cursors_;

    fbl::String manufacturer_name_;
    fbl::String monitor_name_;
    fbl::String monitor_serial_;
  };

  TestFidlClient() {}

  bool CreateChannel(zx_handle_t provider, bool is_vc);
  bool Bind();

  fbl::Vector<Display> displays_;
  std::unique_ptr<::llcpp::fuchsia::hardware::display::Controller::SyncClient> dc_;
  zx::handle device_handle_;
  bool has_ownership_;
};

}  // namespace display

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_DISPLAY_TEST_FIDL_CLIENT_H_
