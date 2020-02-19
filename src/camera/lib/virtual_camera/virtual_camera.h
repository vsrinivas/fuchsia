// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_VIRTUAL_CAMERA_VIRTUAL_CAMERA_H_
#define SRC_CAMERA_LIB_VIRTUAL_CAMERA_VIRTUAL_CAMERA_H_

#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fit/result.h>

namespace camera {

// This class implements a fully configured software-based camera.
class VirtualCamera {
 public:
  virtual ~VirtualCamera() = default;

  // Create a virtual camera using the provided sysmem allocator service handle.
  static fit::result<std::unique_ptr<VirtualCamera>, zx_status_t> Create(
      fidl::InterfaceHandle<fuchsia::sysmem::Allocator> allocator);

  // Returns a request handler for the Device interface.
  virtual fidl::InterfaceRequestHandler<fuchsia::camera3::Device> GetHandler() = 0;

  // Checks the provided buffer for consistency with the provided frame info, returning a
  // descriptive error string on failure.
  virtual fit::result<void, std::string> CheckFrame(const void* data, size_t size,
                                                    const fuchsia::camera3::FrameInfo& info) = 0;
};

}  // namespace camera

#endif  // SRC_CAMERA_LIB_VIRTUAL_CAMERA_VIRTUAL_CAMERA_H_
