// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/sherlock/common_util.h"

namespace camera {

fuchsia::camera2::StreamProperties GetStreamProperties(fuchsia::camera2::CameraStreamType type) {
  fuchsia::camera2::StreamProperties ret{};
  ret.set_stream_type(type);
  return ret;
}

fuchsia::sysmem::BufferCollectionConstraints InvalidConstraints() {
  StreamConstraints stream_constraints;
  stream_constraints.set_bytes_per_row_divisor(0);
  stream_constraints.set_contiguous(false);
  stream_constraints.AddImageFormat(0, 0, fuchsia::sysmem::PixelFormatType::NV12);
  stream_constraints.set_buffer_count_for_camping(0);
  return stream_constraints.MakeBufferCollectionConstraints();
}

}  // namespace camera
