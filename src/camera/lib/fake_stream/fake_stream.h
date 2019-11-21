// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_FAKE_STREAM_FAKE_STREAM_H_
#define SRC_CAMERA_LIB_FAKE_STREAM_FAKE_STREAM_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <lib/fit/result.h>

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "lib/async/dispatcher.h"

namespace camera {

class FakeStream {
 public:
  // Creates a fake stream using the given request, processing events using an optionally provided
  // dispatcher. If dispatcher is omitted or null, uses the current thread's default dispatcher.
  static fit::result<std::unique_ptr<FakeStream>, zx_status_t> Create(
      fidl::InterfaceRequest<fuchsia::camera2::Stream> request,
      async_dispatcher_t* dispatcher = nullptr);

  virtual ~FakeStream() = default;

  // Returns OK if the client is behaving conformantly, or a descriptive error string if it is not.
  virtual fit::result<void, std::string> StreamClientStatus() = 0;

  // Sends the OnFrameAvailable event to the client. Returns ZX_ERR_BAD_STATE if it is currently
  // invalid to send a frame, otherwise returns ZX_OK.
  virtual zx_status_t SendFrameAvailable(fuchsia::camera2::FrameAvailableInfo info) = 0;
};

}  // namespace camera

#endif  // SRC_CAMERA_LIB_FAKE_STREAM_FAKE_STREAM_H_
