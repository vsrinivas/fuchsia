// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_legacy_stream.h"

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/result.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <array>
#include <memory>
#include <sstream>
#include <unordered_set>

namespace camera {

// NOTE: these must be a superset of the formats defined in
// //src/camera/bin/device/test/take_controller.cc
// TODO(fxbug.dev/49137): improve legacy test fakes
static constexpr std::array<fuchsia::sysmem::ImageFormat_2, 3> kFakeImageFormats{{
    {
        .pixel_format =
            {
                .type = fuchsia::sysmem::PixelFormatType::NV12,
            },
        .coded_width = 1920,
        .coded_height = 1080,
        .bytes_per_row = 1920,
        .color_space =
            {
                .type = fuchsia::sysmem::ColorSpaceType::REC601_NTSC,
            },
    },
    {
        .pixel_format =
            {
                .type = fuchsia::sysmem::PixelFormatType::NV12,
            },
        .coded_width = 1280,
        .coded_height = 720,
        .bytes_per_row = 1920,
        .color_space =
            {
                .type = fuchsia::sysmem::ColorSpaceType::REC601_NTSC,
            },
    },
    {
        .pixel_format =
            {
                .type = fuchsia::sysmem::PixelFormatType::NV12,
            },
        .coded_width = 1024,
        .coded_height = 576,
        .bytes_per_row = 1920,
        .color_space =
            {
                .type = fuchsia::sysmem::ColorSpaceType::REC601_NTSC,
            },
    },
}};

class FakeLegacyStreamImpl : public FakeLegacyStream, public fuchsia::camera2::Stream {
 public:
  FakeLegacyStreamImpl() : binding_(this) {}

  void CheckSameThread() {
    auto dispatcher = async_get_default_dispatcher();
    if (dispatcher) {
      ZX_ASSERT_MSG(dispatcher == binding_.dispatcher(), "FakeLegacyStream is not thread-safe!");
    }
  }

  // |camera::FakeLegacyStream|
  fit::result<void, std::string> StreamClientStatus() override {
    if (client_error_count_ == 0) {
      return fit::ok();
    }
    auto error_string = client_error_explanation_.str();
    return fit::error(std::move(error_string));
  }

  zx_status_t SendFrameAvailable(fuchsia::camera2::FrameAvailableInfo info) override {
    CheckSameThread();
    if (stopped_) {
      FX_LOGS(INFO) << "Client has not started the stream";
      return ZX_ERR_BAD_STATE;
    }
    if (outstanding_error_frame_) {
      FX_LOGS(INFO) << "Client has not yet acknowledged a previous error frame";
      return ZX_ERR_BAD_STATE;
    }
    if (info.frame_status == fuchsia::camera2::FrameStatus::OK) {
      auto it = outstanding_buffer_ids_.find(info.buffer_id);
      if (it != outstanding_buffer_ids_.end()) {
        FX_LOGS(INFO) << "Client has not yet returned buffer_id " << info.buffer_id;
        return ZX_ERR_BAD_STATE;
      }
      outstanding_buffer_ids_.insert(info.buffer_id);
    } else {
      outstanding_error_frame_ = true;
    }
    binding_.events().OnFrameAvailable(std::move(info));
    return ZX_OK;
  }

  bool IsStreaming() override {
    CheckSameThread();
    return !stopped_;
  }

  bool IsOutstanding(uint32_t buffer_id) override {
    CheckSameThread();
    return outstanding_buffer_ids_.find(buffer_id) != outstanding_buffer_ids_.end();
  };

 private:
  std::stringstream& ClientErrors() {
    if (client_error_count_ > 0) {
      client_error_explanation_ << std::endl;
    }
    client_error_explanation_ << client_error_count_++ << ": ";
    return client_error_explanation_;
  }

  // |fuchsia::camera2::Stream|
  void Start() override {
    if (!stopped_) {
      ClientErrors() << "Client called Start while stream is already started";
    }
    stopped_ = false;
  }

  void Stop() override {
    if (stopped_) {
      ClientErrors() << "Client called Stop while stream is already stopped";
    }
    stopped_ = true;
  }

  void ReleaseFrame(uint32_t buffer_id) override {
    auto it = outstanding_buffer_ids_.find(buffer_id);
    if (it == outstanding_buffer_ids_.end()) {
      ClientErrors() << "Client called Release with unowned buffer_id " << buffer_id;
    } else {
      outstanding_buffer_ids_.erase(it);
    }
  }

  void AcknowledgeFrameError() override {
    if (!outstanding_error_frame_) {
      ClientErrors()
          << "Client called AcknowledgeFrameError without having received any new error frames";
    }
    outstanding_error_frame_ = false;
  }

  void SetRegionOfInterest(float x_min, float y_min, float x_max, float y_max,
                           SetRegionOfInterestCallback callback) override {
    if (x_min < 0 || y_min < 0 || x_min >= x_max || y_min >= y_max) {
      ClientErrors() << "Client called SetRegionOfInterest with invalid args: " << x_min << ", "
                     << y_min << ", " << x_max << ", " << y_max;
      callback(ZX_ERR_INVALID_ARGS);
    } else {
      callback(ZX_OK);
    }
  }

  void SetImageFormat(uint32_t image_format_index, SetImageFormatCallback callback) override {
    if (image_format_index >= kFakeImageFormats.size()) {
      ClientErrors() << "Client called SetImageFormat with invalid index: " << image_format_index;
      callback(ZX_ERR_INVALID_ARGS);
    } else {
      callback(ZX_OK);
    }
  }

  void GetImageFormats(GetImageFormatsCallback callback) override {
    callback({kFakeImageFormats.begin(), kFakeImageFormats.end()});
  }

  fidl::Binding<fuchsia::camera2::Stream> binding_;
  bool stopped_ = true;
  std::unordered_set<uint32_t> outstanding_buffer_ids_;
  bool outstanding_error_frame_ = false;
  uint32_t client_error_count_ = 0;
  std::stringstream client_error_explanation_;

  friend class FakeLegacyStream;
};

fit::result<std::unique_ptr<FakeLegacyStream>, zx_status_t> FakeLegacyStream::Create(
    fidl::InterfaceRequest<fuchsia::camera2::Stream> request, async_dispatcher_t* dispatcher) {
  auto impl = std::make_unique<FakeLegacyStreamImpl>();
  zx_status_t status = impl->binding_.Bind(std::move(request), dispatcher);
  if (status) {
    return fit::error(status);
  }
  impl->binding_.set_error_handler(
      [impl = impl.get()](zx_status_t status) { impl->ClientErrors() << "Client disconnected"; });
  return fit::ok(std::move(impl));
}

}  // namespace camera
