// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_FACTORY_CAPTURE_H_
#define SRC_CAMERA_BIN_FACTORY_CAPTURE_H_

#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <memory>
#include <string>

namespace camera {

class Capture;
using CaptureResponse = fit::function<void(zx_status_t, std::unique_ptr<Capture>)>;

class Capture {
 public:
  static fit::result<std::unique_ptr<Capture>, zx_status_t> Create(uint32_t stream,
      const std::string path, bool want_image, CaptureResponse callback);
  ~Capture() = default;

  // part of request
  uint32_t stream_;
  bool want_image_;
  CaptureResponse callback_;

  // part of response
  std::unique_ptr<std::basic_string<uint8_t>> image_;    // raw bits if wantImage is true
  fuchsia::camera3::StreamProperties properties_;

 private:
  Capture();
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_FACTORY_CAPTURE_H_
