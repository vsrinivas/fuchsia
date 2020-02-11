// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_FAKE_STREAM_FAKE_STREAM_H_
#define SRC_CAMERA_LIB_FAKE_STREAM_FAKE_STREAM_H_

#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/fit/result.h>
#include <zircon/types.h>

#include <memory>

namespace camera {

// This class provides a mechanism for simulating streams.
class FakeStream {
 public:
  virtual ~FakeStream() = default;

  // Create a fake stream with the given properties.
  static fit::result<std::unique_ptr<FakeStream>, zx_status_t> Create(
      fuchsia::camera3::StreamProperties properties);

  // Returns a request handler for the Stream interface.
  virtual fidl::InterfaceRequestHandler<fuchsia::camera3::Stream> GetHandler() = 0;

  // Sends the given frame info to a Stream client with a pending request. If there are none, it
  // will be queued and returned for future client requests.
  virtual void AddFrame(fuchsia::camera3::FrameInfo info) = 0;
};

}  // namespace camera

#endif  // SRC_CAMERA_LIB_FAKE_STREAM_FAKE_STREAM_H_
