// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_FAKE_CAMERA_FAKE_CAMERA_H_
#define SRC_CAMERA_LIB_FAKE_CAMERA_FAKE_CAMERA_H_

#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fit/result.h>
#include <zircon/types.h>

#include "src/camera/lib/fake_stream/fake_stream.h"

namespace camera {

using FakeConfiguration = std::vector<std::shared_ptr<FakeStream>>;

// This class provides a mechanism for simulating a camera device, controller driver, and content
// streams. All methods are thread-safe.
class FakeCamera {
 public:
  virtual ~FakeCamera() = default;

  // Create a fake camera with the given identifier and configurations.
  static fit::result<std::unique_ptr<FakeCamera>, zx_status_t> Create(
      std::string identifier, std::vector<FakeConfiguration> configurations);

  // Returns a request handler for the Device interface.
  virtual fidl::InterfaceRequestHandler<fuchsia::camera3::Device> GetHandler() = 0;

  // Sets the state of the fake physical mute switch.
  virtual void SetHardwareMuteState(bool muted) = 0;
};

}  // namespace camera

#endif  // SRC_CAMERA_LIB_FAKE_CAMERA_FAKE_CAMERA_H_
