// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DISPLAY_CAPTURE_TEST_RUNNER_H_
#define GARNET_BIN_DISPLAY_CAPTURE_TEST_RUNNER_H_

#include <fuchsia/camera/cpp/fidl.h>
#include <fuchsia/hardware/display/cpp/fidl.h>
#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/synchronous_interface_ptr.h>
#include <lib/fsl/io/device_watcher.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

#include <deque>

#include "context.h"
#include "image.h"
#include "layer.h"
#include "src/lib/files/unique_fd.h"

namespace display_test {

static constexpr uint32_t kMaxFrames = 32;

namespace internal {

class Runner {
 public:
  Runner(async::Loop* loop);
  zx_status_t Start(const char* display_name_);
  void Stop();

  Context* StartTest();
  int32_t CleanupTest();
  static constexpr int32_t kTestOk = 0;
  static constexpr int32_t kTestStatusUnknown = -1;
  static constexpr int32_t kTestDisplayCheckFail = -2;
  static constexpr int32_t kTestVsyncFail = -3;
  static constexpr int32_t kTestCaptureFail = -4;
  static constexpr int32_t kTestCaptureMismatch = -5;

  void ApplyConfig(std::vector<LayerImpl*> layers);

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  zx_pixel_format_t format() const;

  fuchsia::hardware::display::ControllerPtr& display() {
    return display_controller_;
  }
  void OnResourceReady();

 private:
  void OnCameraAvailable(int dir_fd, std::string filename);

  void OnShutdownCallback();
  void GetFormatCallback(::std::vector<fuchsia::camera::VideoFormat> fmts,
                         uint32_t total_count, zx_status_t status);
  void FrameNotifyCallback(const fuchsia::camera::FrameAvailableEvent& resp);

  void InitDisplay();
  void OnDisplaysChanged(::std::vector<fuchsia::hardware::display::Info> added,
                         ::std::vector<uint64_t> removed);
  void OnClientOwnershipChange(bool is_owner);
  void OnVsync(uint64_t display_id, uint64_t timestamp,
               ::std::vector<uint64_t> image_ids);

  void SendFrameConfig(uint32_t frame_idx);
  void CheckFrameConfig(uint32_t frame_idx);
  void ApplyFrame(uint32_t frame_idx);
  bool CheckFrame(uint32_t frame_idx, uint8_t* capture_ptr, bool quick);

  void FinishTest(int32_t status);

  async::Loop* loop_;
  Context runner_context_;
  Image* calibration_image_a_;
  Image* calibration_image_b_;
  PrimaryLayer* calibration_layer_;

  const char* display_name_;
  zx::channel display_controller_conn_;
  fuchsia::hardware::display::ControllerPtr display_controller_;
  uint64_t display_id_ = 0;

  bool display_ownership_ = false;
  bool camera_setup_ = false;
  std::unique_ptr<fsl::DeviceWatcher> camera_watcher_;

  fuchsia::camera::ControlPtr camera_control_;
  fuchsia::camera::StreamPtr camera_stream_;
  zx::eventpair stream_token_;
  uint8_t* camera_buffers_[kMaxFrames];
  uint32_t camera_stride_;
  uint32_t width_;
  uint32_t height_;

  std::unique_ptr<Context> test_context_;
  std::vector<std::vector<std::pair<LayerImpl*, const void*>>> frames_;
  uint32_t display_idx_ = 0;
  uint32_t capture_idx_ = 0;
  uint32_t bad_capture_count_ = 0;
  bool test_running_ = false;
  int32_t test_status_ = kTestStatusUnknown;

  std::vector<uint32_t> buffer_ids_;
};

}  // namespace internal
}  // namespace display_test

#endif  // GARNET_BIN_DISPLAY_CAPTURE_TEST_RUNNER_H_
