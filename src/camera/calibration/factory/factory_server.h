// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_CALIBRATION_FACTORY_PROTOCOL_FACTORY_PROTOCOL_H_
#define SRC_CAMERA_CALIBRATION_FACTORY_PROTOCOL_FACTORY_PROTOCOL_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/factory/camera/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>

namespace camera {

// The server-side implementation for the factory calibrations API. Also acts as a stream client and
// servers as the middle layer between calls from the factory host and several layers in the camera
// stack.
class FactoryServer : public fuchsia::camera2::Stream_EventSender,
                      fuchsia::factory::camera::CameraFactory {
 public:
  FactoryServer() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}
  ~FactoryServer() override {
    if (controller_) {
      controller_.Unbind();
    }
    loop_.Shutdown();
  }

  // Factory method that creates a FactoryServer and connects it to the ISP Tester Driver.
  // Args:
  //   |channel|: The channel to bind.
  // Returns:
  //  A FactoryServer object which acts as an interface to the factory calibrations API.
  static fit::result<std::unique_ptr<FactoryServer>, zx_status_t> Create(
      fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller);

  // Creates a new Controller stream, and instantiates an ImageWriterUtil to track the Stream's
  // buffers.
  void ConnectToStream();

  // Getters
  bool frames_received() const { return frames_received_; }
  bool streaming() const { return streaming_; }

 private:
  // |fuchsia::camera2::Stream|
  void OnFrameAvailable(fuchsia::camera2::FrameAvailableInfo info) override;

  // |fuchsia::factory::camera::CameraFactory|
  void DetectCamera(DetectCameraCallback callback) override;
  void Start() override;
  void Stop() override;
  void SetConfig(uint32_t mode, int32_t integration_time, int32_t analog_gain, int32_t digital_gain,
                 SetConfigCallback callback) override;
  void CaptureImage(CaptureImageCallback callback) override;
  void WriteCalibrationData(fuchsia::mem::Buffer calibration_data, std::string file_path,
                            WriteCalibrationDataCallback callback) override;

  async::Loop loop_;
  fuchsia::camera2::hal::ControllerPtr controller_;
  bool streaming_ = false;
  bool frames_received_ = false;
};

}  // namespace camera

#endif  // SRC_CAMERA_CALIBRATION_FACTORY_PROTOCOL_FACTORY_PROTOCOL_H_
