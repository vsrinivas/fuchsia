// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_EXAMPLES_DEMO_STREAMPTR_WRAPPER_H_
#define SRC_CAMERA_EXAMPLES_DEMO_STREAMPTR_WRAPPER_H_

#include <fuchsia/camera2/cpp/fidl.h>

// This is a simple wrapper that forwards all Stream methods to an owned StreamPtr instance. It can
// be used to serve the Stream interface as a std::unique_ptr.
class StreamPtrWrapper : public fuchsia::camera2::Stream {
 public:
  StreamPtrWrapper(fuchsia::camera2::StreamPtr stream) : stream_(std::move(stream)) {}
  fuchsia::camera2::StreamPtr& operator->() { return stream_; }
  virtual void Start() override { stream_->Start(); }
  virtual void Stop() override { stream_->Stop(); }
  virtual void ReleaseFrame(uint32_t buffer_id) override { stream_->ReleaseFrame(buffer_id); }
  virtual void AcknowledgeFrameError() override { stream_->AcknowledgeFrameError(); }
  virtual void SetRegionOfInterest(float x_min, float y_min, float x_max, float y_max,
                                   SetRegionOfInterestCallback callback) override {
    stream_->SetRegionOfInterest(x_min, y_min, x_max, y_max, std::move(callback));
  }
  virtual void SetImageFormat(uint32_t image_format_index,
                              SetImageFormatCallback callback) override {
    stream_->SetImageFormat(image_format_index, std::move(callback));
  }
  virtual void GetImageFormats(GetImageFormatsCallback callback) override {
    stream_->GetImageFormats(std::move(callback));
  }

 private:
  fuchsia::camera2::StreamPtr stream_;
};

#endif  // SRC_CAMERA_EXAMPLES_DEMO_STREAMPTR_WRAPPER_H_
