// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/util/debug_print.h"

// These should only be called by operator<<, so it's safe to elide them.
#if !defined(NDEBUG)

#include "escher/geometry/bounding_box.h"
#include "escher/geometry/transform.h"
#include "escher/impl/model_pipeline_spec.h"
#include "escher/renderer/image.h"
#include "escher/scene/viewing_volume.h"

namespace escher {

std::ostream& operator<<(std::ostream& str, const Transform& transform) {
  return str << "Transform[t: " << transform.translation
             << " s: " << transform.scale << " r: " << transform.rotation
             << " a: " << transform.anchor << "]";
}

std::ostream& operator<<(std::ostream& str, const mat2& m) {
  str << "mat2[";
  for (int y = 0; y < 2; ++y) {
    str << std::endl;
    for (int x = 0; x < 2; ++x) {
      str << " " << m[x][y];
    }
  }
  return str << " ]";
}

std::ostream& operator<<(std::ostream& str, const mat4& m) {
  str << "mat4[";
  for (int y = 0; y < 4; ++y) {
    str << std::endl;
    for (int x = 0; x < 4; ++x) {
      str << " " << m[x][y];
    }
  }
  return str << " ]";
}

std::ostream& operator<<(std::ostream& str, const vec2& v) {
  return str << "(" << v[0] << ", " << v[1] << ")";
}

std::ostream& operator<<(std::ostream& str, const vec3& v) {
  return str << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
}

std::ostream& operator<<(std::ostream& str, const vec4& v) {
  return str << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3]
             << ")";
}

std::ostream& operator<<(std::ostream& str, const quat& q) {
  return str << "(" << q.x << ", " << q.y << ", " << q.z << ", " << q.w << ")";
}

std::ostream& operator<<(std::ostream& str, const MeshAttribute& attr) {
  switch (attr) {
    case MeshAttribute::kPosition:
      str << "kPosition";
      break;
    case MeshAttribute::kPositionOffset:
      str << "kPositionOffset";
      break;
    case MeshAttribute::kUV:
      str << "kUV";
      break;
    case MeshAttribute::kPerimeterPos:
      str << "kPerimeterPos";
      break;
    case MeshAttribute::kStride:
      str << "kStride";
      break;
  }
  return str;
}

std::ostream& operator<<(std::ostream& str, const MeshSpec& spec) {
  bool has_flag = false;
  str << "MeshSpec[";
  // TODO: would be nice to guarantee that we don't miss any.  Too bad we can't
  // enumerate over the values in an enum class.
  std::array<MeshAttribute, 4> all_flags = {
      {MeshAttribute::kPosition, MeshAttribute::kPositionOffset,
       MeshAttribute::kUV, MeshAttribute::kPerimeterPos}};
  for (auto flag : all_flags) {
    if (spec.flags & flag) {
      // Put a pipe after the previous flag, if there is one.
      if (has_flag) {
        str << "|";
      } else {
        has_flag = true;
      }
      str << flag;
    }
  }
  str << "]";
  return str;
}

std::ostream& operator<<(std::ostream& str,
                         const impl::ModelPipelineSpec& spec) {
  str << "ModelPipelineSpec[" << spec.mesh_spec << ", " << spec.shape_modifiers
      << ", sample_count: " << spec.sample_count
      << ", depth_prepass: " << spec.use_depth_prepass << "]";
  return str;
}

std::ostream& operator<<(std::ostream& str, const ShapeModifier& flag) {
  switch (flag) {
    case ShapeModifier::kWobble:
      str << "kWobble";
      break;
  }
  return str;
}

std::ostream& operator<<(std::ostream& str, const ShapeModifiers& flags) {
  bool has_flag = false;
  str << "ShapeModifiers[";
  // TODO: would be nice to guarantee that we don't miss any.  Too bad we can't
  // enumerate over the values in an enum class.
  std::array<ShapeModifier, 1> all_flags = {{ShapeModifier::kWobble}};
  for (auto flag : all_flags) {
    if (flags & flag) {
      // Put a pipe after the previous flag, if there is one.
      if (has_flag) {
        str << "|";
      } else {
        has_flag = true;
      }
      str << flag;
    }
  }
  str << "]";
  return str;
}

std::ostream& operator<<(std::ostream& str, const ImageInfo& info) {
  return str << "ImageInfo[" << info.width << "x" << info.height << " "
             << vk::to_string(info.format) << "  samples: " << info.sample_count
             << "]";
}

std::ostream& operator<<(std::ostream& str, const ViewingVolume& volume) {
  return str << "ViewingVolume[w:" << volume.width() << " h:" << volume.height()
             << " t:" << volume.top() << " b:" << volume.bottom() << "]";
}

std::ostream& operator<<(std::ostream& str, const BoundingBox& box) {
  if (box.is_empty()) {
    return str << "BoundingBox[empty]";
  } else {
    return str << "BoundingBox[min" << box.min() << ", max" << box.max() << "]";
  }
}

}  // namespace escher

#endif  // #if !defined(NDEBUG)
