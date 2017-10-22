// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/util/debug_print.h"

// These should only be called by operator<<, so it's safe to elide them.
#if !defined(NDEBUG)

#include "lib/escher/geometry/bounding_box.h"
#include "lib/escher/geometry/transform.h"
#include "lib/escher/impl/model_pipeline_spec.h"
#include "lib/escher/scene/camera.h"
#include "lib/escher/scene/viewing_volume.h"
#include "lib/escher/vk/image.h"

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
    case MeshAttribute::kPosition2D:
      str << "kPosition2D";
      break;
    case MeshAttribute::kPosition3D:
      str << "kPosition3D";
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
  std::array<MeshAttribute, 5> all_flags = {
      {MeshAttribute::kPosition2D, MeshAttribute::kPosition3D,
       MeshAttribute::kPositionOffset, MeshAttribute::kUV,
       MeshAttribute::kPerimeterPos}};
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

std::ostream& operator<<(
    std::ostream& str,
    const impl::ModelPipelineSpec::ClipperState& clipper_state) {
  using ClipperState = impl::ModelPipelineSpec::ClipperState;
  switch (clipper_state) {
    case ClipperState::kBeginClipChildren:
      str << "ClipperState::kBeginClipChildren";
      break;
    case ClipperState::kEndClipChildren:
      str << "ClipperState::kEndClipChildren";
      break;
    case ClipperState::kNoClipChildren:
      str << "ClipperState::kNoClipChildren";
      break;
  }
  return str;
}

std::ostream& operator<<(std::ostream& str,
                         const impl::ModelPipelineSpec& spec) {
  str << "ModelPipelineSpec[" << spec.mesh_spec << ", " << spec.shape_modifiers
      << ", sample_count: " << spec.sample_count
      << ", clipper_state: " << spec.clipper_state
      << ", is_clippee: " << spec.is_clippee
      << ", depth_prepass: " << spec.use_depth_prepass
      << ", has_material: " << spec.has_material
      << ", is_opaque: " << spec.is_opaque << "]";
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

std::ostream& operator<<(std::ostream& str, const Camera& camera) {
  return str << "Camera[\ntransform: " << camera.transform()
             << "\nprojection: " << camera.projection() << "]";
}

}  // namespace escher

#endif  // #if !defined(NDEBUG)
