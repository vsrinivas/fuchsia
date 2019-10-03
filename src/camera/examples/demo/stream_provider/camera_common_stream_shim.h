// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_EXAMPLES_DEMO_CAMERA_COMMON_STREAM_SHIM_H_
#define SRC_CAMERA_EXAMPLES_DEMO_CAMERA_COMMON_STREAM_SHIM_H_

#include <fuchsia/camera/common/cpp/fidl.h>
#include <fuchsia/camera2/cpp/fidl.h>

#include <memory>

namespace camera {

// This class provides a shim to serve a fuchsia::camera2::Stream interface to a caller using a
// fuchsia::camera::common::Stream client connection.
class CameraCommonStreamShim : public fuchsia::camera2::Stream {
 public:
  CameraCommonStreamShim(fuchsia::camera::common::StreamPtr stream,
                         fuchsia::camera2::Stream_EventSender* event_handler)
      : stream_(std::move(stream)), event_handler_(event_handler) {
    stream_.events().OnFrameAvailable = [this](fuchsia::camera::common::FrameAvailableEvent event) {
      event_handler_->OnFrameAvailable(Convert(event));
    };
  }
  virtual void Start() override { stream_->Start(); }
  virtual void Stop() override { stream_->Stop(); }
  virtual void ReleaseFrame(uint32_t buffer_id) override { stream_->ReleaseFrame(buffer_id); }
  virtual void AcknowledgeFrameError() override {}
  virtual void SetRegionOfInterest(float x_min, float y_min, float x_max, float y_max,
                                   SetRegionOfInterestCallback callback) override {
    callback(ZX_ERR_NOT_SUPPORTED);
  }
  virtual void SetImageFormat(uint32_t image_format_index,
                              SetImageFormatCallback callback) override {
    callback(ZX_ERR_NOT_SUPPORTED);
  }
  virtual void GetImageFormats(GetImageFormatsCallback callback) override {
    callback(std::vector<fuchsia::sysmem::ImageFormat_2>());
  }
  static fuchsia::camera2::FrameAvailableInfo Convert(
      fuchsia::camera::common::FrameAvailableEvent x) {
    fuchsia::camera2::FrameAvailableInfo ret;
    ret.frame_status = Convert(x.frame_status);
    ret.buffer_id = x.buffer_id;
    ret.metadata = Convert(x.metadata);
    return ret;
  }
  static fuchsia::camera2::FrameStatus Convert(fuchsia::camera::common::FrameStatus frame_status) {
    switch (frame_status) {
      case fuchsia::camera::common::FrameStatus::OK:
        return fuchsia::camera2::FrameStatus::OK;
      case fuchsia::camera::common::FrameStatus::ERROR_FRAME:
        return fuchsia::camera2::FrameStatus::ERROR_FRAME;
      case fuchsia::camera::common::FrameStatus::ERROR_BUFFER_FULL:
        return fuchsia::camera2::FrameStatus::ERROR_BUFFER_FULL;
      default:
        FXL_LOG(ERROR) << "Inconvertible Value " << static_cast<uint32_t>(frame_status);
        return fuchsia::camera2::FrameStatus::OK;
    }
  }
  static fuchsia::camera2::FrameMetadata Convert(fuchsia::camera::common::Metadata metadata) {
    fuchsia::camera2::FrameMetadata ret;
    ret.set_timestamp(metadata.timestamp);
    return ret;
  }

 private:
  fuchsia::camera::common::StreamPtr stream_;
  fuchsia::camera2::Stream_EventSender* event_handler_;
};

}  // namespace camera

#endif  // SRC_CAMERA_EXAMPLES_DEMO_CAMERA_COMMON_STREAM_SHIM_H_
