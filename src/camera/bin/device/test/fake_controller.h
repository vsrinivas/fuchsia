
// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_DEVICE_TEST_FAKE_CONTROLLER_H_
#define SRC_CAMERA_BIN_DEVICE_TEST_FAKE_CONTROLLER_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/result.h>
#include <zircon/status.h>

#include <memory>

class FakeController : public fuchsia::camera2::hal::Controller {
 public:
  FakeController();
  ~FakeController();
  static fit::result<std::unique_ptr<FakeController>, zx_status_t> Create(
      fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> request);

 private:
  // |fuchsia::camera2::hal::Controller|
  void GetConfigs(fuchsia::camera2::hal::Controller::GetConfigsCallback callback) override;
  void CreateStream(uint32_t config_index, uint32_t stream_index, uint32_t image_format_index,
                    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection,
                    fidl::InterfaceRequest<fuchsia::camera2::Stream> stream) override;
  void EnableStreaming() override;
  void DisableStreaming() override;
  void GetDeviceInfo(fuchsia::camera2::hal::Controller::GetDeviceInfoCallback callback) override;

  async::Loop loop_;
  fidl::Binding<fuchsia::camera2::hal::Controller> binding_;
};

#endif  // SRC_CAMERA_BIN_DEVICE_TEST_FAKE_CONTROLLER_H_
