// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include <lib/fit/function.h>
#include <zircon/device/camera.h>
#include <zircon/status.h>
#include <zircon/types.h>
#include <zx/vmo.h>

namespace video_display {

using SetFormatCallback = fit::function<zx_status_t(uint64_t)>;
using OnShutdownCallback = fit::function<void()>;
using GetFormatCallback = fit::function<zx_status_t(
    const std::vector<camera_video_format_t>& out_formats)>;
using FrameNotifyCallback =
    fit::function<zx_status_t(camera_vb_frame_notify_t)>;

// An abstract class which provides the same interface as the camera driver api.
// See camera_client.h for a more detailed descritption of how to use this
// interface.
class CameraInterfaceBase {
 public:
  virtual zx_status_t SetFormat(const camera_video_format_t& format,
                                SetFormatCallback set_format_callback) = 0;
  virtual zx_status_t GetSupportedFormats(
      GetFormatCallback get_formats_callback) = 0;
  virtual zx_status_t SetBuffer(const zx::vmo& vmo) = 0;
  virtual zx_status_t Start(FrameNotifyCallback frame_notify_callback) = 0;
  virtual zx_status_t ReleaseFrame(uint64_t data_offset) = 0;
  virtual zx_status_t Stop() = 0;
  // The callback on shutdown may be called from an arbitrary thread, depending
  // on implimentation. See the derived class for more detail.
  virtual zx_status_t Open(uint32_t dev_id, OnShutdownCallback callback) = 0;
  virtual void Close() = 0;
  virtual ~CameraInterfaceBase() {}
};

}  // namespace video_display
