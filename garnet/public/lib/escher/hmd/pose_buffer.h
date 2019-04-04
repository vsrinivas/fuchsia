// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_HMD_POSE_BUFFER_H_
#define LIB_ESCHER_HMD_POSE_BUFFER_H_

#include "lib/escher/vk/buffer.h"

#include "garnet/lib/ui/util/glm_workaround.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace escher {
namespace hmd {

// A packed combination of a quaternion and a 3D position intended to be packed
// into a PoseBuffer for late latched head tracking applications. See
// commands.fidl for details on PoseBuffer
struct Pose {
  Pose(glm::quat quaternion, glm::vec3 position) {
    a = quaternion.x;
    b = quaternion.y;
    c = quaternion.z;
    d = quaternion.w;

    x = position.x;
    y = position.y;
    z = position.z;

    for (size_t i = 0; i < kReservedBytes; i++)
      reserved[i] = 0;
  }

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
  static constexpr size_t kReservedBytes = 4;
  uint8_t reserved[kReservedBytes];
};

static_assert(sizeof(Pose) == 32, "Pose structure is not 32 bytes");

struct PoseBuffer {
  PoseBuffer() {}
  PoseBuffer(BufferPtr buffer, uint32_t num_entries, int64_t base_time,
             int64_t time_interval)
      : buffer(buffer),
        num_entries(num_entries),
        base_time(base_time),
        time_interval(time_interval) {}
  BufferPtr buffer;
  uint32_t num_entries = 0;
  int64_t base_time = 0;
  int64_t time_interval = 0;

  operator bool() const { return !!buffer; }
};

}  // namespace hmd
}  // namespace escher

#endif  // LIB_ESCHER_HMD_POSE_BUFFER_H_
