// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "controller_error.h"

#include <fuchsia/camera/gym/cpp/fidl.h>

namespace camera {

std::string CommandErrorString(fuchsia::camera::gym::CommandError status) {
  switch (status) {
    case fuchsia::camera::gym::CommandError::OUT_OF_RANGE:
      return std::string(kCommandErrorOutOfRange);
    default:
      ZX_ASSERT(false);  // Should never get here.
  }
}

}  // namespace camera
