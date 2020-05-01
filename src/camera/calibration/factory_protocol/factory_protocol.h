// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_CALIBRATION_FACTORY_PROTOCOL_FACTORY_PROTOCOL_H_
#define SRC_CAMERA_CALIBRATION_FACTORY_PROTOCOL_FACTORY_PROTOCOL_H_

#include <fuchsia/camera/test/cpp/fidl.h>
#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/factory/camera/cpp/fidl.h>
#include <fuchsia/hardware/camera/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include "src/camera/lib/stream_utils/image_io_util.h"

namespace camera {

// The server-side implementation for the factory calibrations API. Also acts as a stream client and
// servers as the middle layer between calls from the factory host and several layers in the camera
// stack.
class FactoryProtocol : public fuchsia::camera2::Stream_EventSender,
                        fuchsia::factory::camera::CameraFactory {
 public:
  explicit FactoryProtocol(async_dispatcher_t* dispatcher)
      : binding_(this), dispatcher_(dispatcher) {}
  ~FactoryProtocol() override { Shutdown(ZX_OK); }

  // Factory method that creates a FactoryProtocol and connects it to the ISP Tester Driver.
  // Args:
  //   |channel|: The channel to bind.
  // Returns:
  //  A FactoryProtocol object which acts as an interface to the factory calibrations API.
  static fit::result<std::unique_ptr<FactoryProtocol>, zx_status_t> Create(
      zx::channel channel, async_dispatcher_t* dispatcher);

  // Binds a channel to the ISP Tester driver, creates a new Stream from the driver, and
  // instantiates an ImageWriterUtil to track the Stream's buffers.
  // Returns:
  //  A status values indicating whether stream creation succeeded.
  zx_status_t ConnectToStream();

  // Stop the Stream if it is running. Otherwise do nothing.
  void Shutdown(zx_status_t status);

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

  fidl::Binding<fuchsia::factory::camera::CameraFactory> binding_;
  fuchsia::camera2::hal::ControllerSyncPtr controller_;
  fuchsia::camera::test::IspTesterSyncPtr isp_tester_;
  async_dispatcher_t* dispatcher_;
  std::unique_ptr<ImageIOUtil> image_io_util_;
  fuchsia::camera2::StreamPtr stream_;
  bool streaming_ = false;
  bool frames_received_ = false;
  bool write_allowed_ = true;
};

}  // namespace camera

#endif  // SRC_CAMERA_CALIBRATION_FACTORY_PROTOCOL_FACTORY_PROTOCOL_H_
