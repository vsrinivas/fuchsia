// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_DEVICE_SIZE_EQUAL_H_
#define SRC_CAMERA_BIN_DEVICE_SIZE_EQUAL_H_

#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>

namespace camera {

fuchsia::math::Size ConvertToSize(fuchsia::sysmem::ImageFormat_2 format);
bool SizeEqual(fuchsia::math::Size a, fuchsia::math::Size b);

}  // namespace camera

#endif  // SRC_CAMERA_BIN_DEVICE_SIZE_EQUAL_H_
