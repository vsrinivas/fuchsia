// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

namespace scenic_lib {

struct Pose {
  // Quaternion
  float a;
  float b;
  float c;
  float d;

  // Position
  float x;
  float y;
  float z;

  // Reserved/Padding
  uint8_t reserved[4];
};

}  // namespace scenic_lib