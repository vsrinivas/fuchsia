// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/factory/capture.h"

namespace camera {

fit::result<std::unique_ptr<Capture>, zx_status_t> Capture::Create(uint32_t stream,
    const std::string path, bool want_image, CaptureResponse callback) {
  auto capture = std::unique_ptr<Capture>(new Capture);
  capture->stream_ = stream;
  capture->want_image_ = want_image;
  capture->image_ = std::make_unique<std::basic_string<uint8_t>>();
  capture->callback_ = std::move(callback);
  return fit::ok(std::move(capture));
}

Capture::Capture() {}

}  // namespace camera
