// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/ui/input/gesture.h"

namespace input {

Gesture::Delta& Gesture::Delta::operator+=(const Delta& other) {
  translation += other.translation;
  rotation += other.rotation;
  scale *= other.scale;
  return *this;
}

bool Gesture::Delta::operator==(const Delta& other) const {
  return translation == other.translation && rotation == other.rotation && scale == other.scale;
}

void Gesture::AddPointer(PointerId pointer_id, const glm::vec2& position) {
  // TODO(fxbug.dev/24596): This is sometimes violated.
  // FX_DCHECK(pointers_.find(pointer_id) == pointers_.end());

  pointers_[pointer_id] = {.absolute = position, .relative = {}, .distance = 0};
  UpdateCentroid();
  UpdateRelative();
}

Gesture::Delta Gesture::UpdatePointer(PointerId pointer_id, const glm::vec2& position) {
  // TODO(fxbug.dev/24596): This is sometimes violated.
  // FX_DCHECK(pointers_.find(pointer_id) != pointers_.end());
  auto it = pointers_.find(pointer_id);
  if (it == pointers_.end()) {
    AddPointer(pointer_id, position);
    return {.translation = {}, .rotation = 0, .scale = 1};
  }

  it->second.absolute = position;

  Delta delta;

  {
    glm::vec2 old_centroid = centroid_;
    UpdateCentroid();
    delta.translation = centroid_ - old_centroid;
  }

  if (pointers_.size() > 1) {
    float moment_sum = 0;
    // Use an arithmetic mean as a decent approximation of a geometric mean.
    float scale_sum = 0;

    for (auto& entry : pointers_) {
      PointerInfo& p = entry.second;

      glm::vec2 old_relative = p.relative;
      float old_distance = p.distance;
      p.relative = p.absolute - centroid_;
      p.distance = glm::length(p.relative);

      // TODO(rosswang): The following has singular behavior when pointers have the same
      // coordinates, which can in particular happen during testing. In such cases while it makes
      // mathematical sense for rotation and scale to be NaN/infinite, perhaps we should default
      // them to identity values.

      // For small displacements, this approximates radians.
      delta.rotation +=
          (old_relative.x * p.relative.y - old_relative.y * p.relative.x) / old_distance;
      moment_sum += old_distance;
      scale_sum += p.distance / old_distance;
    }

    delta.rotation /= moment_sum;
    delta.scale *= scale_sum / pointers_.size();
  }

  return delta;
}

void Gesture::RemovePointer(PointerId pointer_id) {
  pointers_.erase(pointer_id);

  if (!pointers_.empty()) {
    UpdateCentroid();
    UpdateRelative();
  }
}

void Gesture::UpdateCentroid() {
  // It would be more efficient to do this incrementally at the possible cost of precision. Gestures
  // tend to be both short and with a small number of pointers, so neither the efficiency nor
  // precision is particularly important. However, edge cases like new pointers are easier to deal
  // with if we always recalculate, especially with fxbug.dev/24596.
  centroid_ = {0, 0};
  for (const auto& entry : pointers_) {
    centroid_ += entry.second.absolute;
  }
  centroid_ /= pointers_.size();
}

void Gesture::UpdateRelative() {
  for (auto& entry : pointers_) {
    PointerInfo& p = entry.second;
    p.relative = p.absolute - centroid_;
    p.distance = glm::length(p.relative);
  }
}

}  // namespace input
