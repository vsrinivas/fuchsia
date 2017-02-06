// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/util/debug_print.h"

// These should only be called by operator<<, so it's safe to elide them.
#if !defined(NDEBUG)

#include "escher/impl/model_pipeline_spec.h"

namespace escher {

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
  }
  return str;
}

std::ostream& operator<<(std::ostream& str, const MeshSpec& spec) {
  bool has_flag = false;
  str << "MeshSpec[";
  // TODO: would be nice to guarantee that we don't miss any.  Too bad we can't
  // enumerate over the values in an enum class.
  std::array<MeshAttribute, 4> all_flags = {{
      MeshAttribute::kPosition, MeshAttribute::kPositionOffset,
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

}  // namespace escher

#endif  // #if !defined(NDEBUG)
