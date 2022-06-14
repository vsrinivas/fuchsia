// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "screen_capture2.h"

#include <lib/syslog/cpp/macros.h>

#include <utility>

using fuchsia::ui::composition::internal::ScreenCaptureConfig;
using fuchsia::ui::composition::internal::ScreenCaptureError;

namespace screen_capture2 {

ScreenCapture::ScreenCapture(
    fidl::InterfaceRequest<fuchsia::ui::composition::internal::ScreenCapture> request)
    : binding_(this, std::move(request)) {}

ScreenCapture::~ScreenCapture() {}

void ScreenCapture::Configure(ScreenCaptureConfig args, ConfigureCallback callback) {
  if (!args.has_image_size() || !args.has_import_token()) {
    FX_LOGS(WARNING) << "ScreenCapture::Configure: Missing arguments.";
    callback(fpromise::error(ScreenCaptureError::MISSING_ARGS));
    return;
  }

  if (!args.image_size().width || !args.image_size().height) {
    FX_LOGS(WARNING) << "ScreenCapture::Configure: Invalid arguments.";
    callback(fpromise::error(ScreenCaptureError::INVALID_ARGS));
    return;
  }

  callback(fpromise::ok());
}

void ScreenCapture::GetNextFrame(ScreenCapture::GetNextFrameCallback callback) {
  FX_NOTIMPLEMENTED();
}

}  // namespace screen_capture2
