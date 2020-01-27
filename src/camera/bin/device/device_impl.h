// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_DEVICE_DEVICE_IMPL_H_
#define SRC_CAMERA_BIN_DEVICE_DEVICE_IMPL_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/result.h>
#include <zircon/status.h>

#include <memory>
#include <vector>

class DeviceImpl {
 public:
  DeviceImpl();
  ~DeviceImpl();
  static fit::result<std::unique_ptr<DeviceImpl>, zx_status_t> Create(
      fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller);

 private:
  void OnControllerDisconnected(zx_status_t status);

  class Client : public fuchsia::camera3::Device {
   public:
    Client();
    static fit::result<std::unique_ptr<Client>, zx_status_t> Create();

   private:
    fidl::Binding<fuchsia::camera3::Device> binding_;
  };

  async::Loop loop_;
  fuchsia::camera2::hal::ControllerPtr controller_;
  fuchsia::camera2::DeviceInfo device_info_;
  std::vector<fuchsia::camera2::hal::Config> configs_;
};

#endif  // SRC_CAMERA_BIN_DEVICE_DEVICE_IMPL_H_
