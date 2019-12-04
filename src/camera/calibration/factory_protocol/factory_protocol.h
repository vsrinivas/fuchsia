// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_CALIBRATION_FACTORY_PROTOCOL_FACTORY_PROTOCOL_H_
#define SRC_CAMERA_CALIBRATION_FACTORY_PROTOCOL_FACTORY_PROTOCOL_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/factory/camera/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <fbl/unique_fd.h>

#include "src/camera/stream_utils/image_io_util.h"

namespace camera {

// The server-side implementation for the factory calibrations API. Also acts as a stream client and
// servers as the middle layer between calls from the factory host and several layers in the camera
// stack.
class FactoryProtocol : public fuchsia::camera2::Stream_EventSender,
                        fuchsia::factory::camera::CameraFactory {
 public:
  FactoryProtocol() : loop_(&kAsyncLoopConfigAttachToCurrentThread) {}
  ~FactoryProtocol() override { StopStream(); }

  // Factory method that creates a FactoryProtocol and connects it to the ISP Tester Driver.
  // Returns:
  //  A FactoryProtocol object which acts as an interface to the factory calibrations API.
  static std::unique_ptr<FactoryProtocol> Create();

  // Binds a channel to the ISP Tester driver, creates a new Stream from the driver, and
  // instantiates an ImageWriterUtil to track the Stream's buffers.
  // Returns:
  //  A status values indicating whether stream creation succeeded.
  zx_status_t ConnectToStream();

  // Stop the Stream if it is running. Otherwise do nothing.
  void StopStream();

 private:
  // |fuchsia::camera2::Stream|
  void OnFrameAvailable(fuchsia::camera2::FrameAvailableInfo info) override;

  // |fuchsia::factory::camera::CameraFactory|
  void DetectCamera(DetectCameraCallback callback) override;
  void Start() override;
  void Stop() override;
  void SetConfig(uint32_t mode, uint32_t exposure, int32_t analog_gain, int32_t digital_gain,
                 SetConfigCallback callback) override;
  void CaptureImage(CaptureImageCallback callback) override;
  void WriteCalibrationData(fuchsia::mem::Buffer calibration_data, std::string file_path,
                            WriteCalibrationDataCallback callback) override;

  fbl::unique_fd isp_fd_;
  async::Loop loop_;
  fuchsia::camera2::StreamPtr stream_;
  std::unique_ptr<ImageIOUtil> image_io_util_;

  bool streaming_ = false;
};

}  // namespace camera

#endif  // SRC_CAMERA_CALIBRATION_FACTORY_PROTOCOL_FACTORY_PROTOCOL_H_
