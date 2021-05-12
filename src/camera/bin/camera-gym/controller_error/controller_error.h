// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_CAMERA_GYM_CONTROLLER_ERROR_CONTROLLER_ERROR_H_
#define SRC_CAMERA_BIN_CAMERA_GYM_CONTROLLER_ERROR_CONTROLLER_ERROR_H_

#include <fuchsia/camera/gym/cpp/fidl.h>

namespace camera {

const char kCommandErrorOutOfRange[] = "Out of range";

std::string CommandErrorString(fuchsia::camera::gym::CommandError status);

}  // namespace camera

#endif  // SRC_CAMERA_BIN_CAMERA_GYM_CONTROLLER_ERROR_CONTROLLER_ERROR_H_
