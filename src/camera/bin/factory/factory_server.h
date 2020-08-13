// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_FACTORY_FACTORY_SERVER_H_
#define SRC_CAMERA_BIN_FACTORY_FACTORY_SERVER_H_

#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/factory/camera/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include "src/camera/bin/factory/streamer.h"
#include "src/camera/bin/factory/web_ui.h"

namespace camera {

// The server-side implementation for the factory API. It maintains connections to scenic,
// camera3 and other services via other classes as needed.
//
// More specifically, it acts as a stream client and serves as the middle layer between calls
// from the factory host and several layers in the camera stack.
class FactoryServer : public fuchsia::factory::camera::Controller, WebUIControl {
 public:
  FactoryServer();
  ~FactoryServer();

  // Factory method that creates a FactoryServer and connects it to the Camera Manager and ISP
  // driver.
  //
  // Returns:
  //  A FactoryServer object which provides an interface to the factory API.
  static fit::result<std::unique_ptr<FactoryServer>, zx_status_t> Create(
      std::unique_ptr<Streamer> streamer, fit::closure stop_callback = nullptr);

  // Getters
  bool streaming() const { return streaming_; }

  void StartStream() {}
  void StopStream() {}
  void CaptureFrames(uint32_t amount, std::string dir_path) {}
  void DisplayToScreen(uint32_t stream_index) {}
  void GetOtpData();
  void GetSensorTemperature();
  void SetAWBMode(fuchsia::factory::camera::WhiteBalanceMode mode, uint32_t temp);
  void SetAEMode(fuchsia::factory::camera::ExposureMode mode);
  void SetExposure(float integration_time, float analog_gain, float digital_gain);
  void SetSensorMode(uint32_t mode);
  void SetTestPatternMode(uint16_t mode);

 private:
  // |fuchsia.camera.factory.Controller|
  void StartStreaming() override {}
  void StopStreaming() override {}
  void CaptureFrames(uint32_t amount, std::string dir_path, CaptureFramesCallback cb) override {}
  void DisplayToScreen(uint32_t stream_index, DisplayToScreenCallback cb) override {}
  void BindIspChannel(fidl::InterfaceRequest<fuchsia::factory::camera::Isp> isp_req) override {}

  // |WebUIControl|
  void RequestCaptureData(uint32_t stream_index, CaptureResponse callback) override;

  async::Loop loop_;
  fit::closure stop_callback_;
  fuchsia::factory::camera::IspPtr isp_;
  std::unique_ptr<Streamer> streamer_;
  std::unique_ptr<WebUI> webui_;
  bool streaming_ = false;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_FACTORY_FACTORY_SERVER_H_
