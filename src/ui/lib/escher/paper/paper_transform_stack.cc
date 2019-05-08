// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/paper/paper_transform_stack.h"

#include "src/ui/lib/escher/geometry/plane_ops.h"
#include "src/ui/lib/escher/util/trace_macros.h"

namespace escher {
PaperTransformStack::PaperTransformStack() = default;

const PaperTransformStack::Item& PaperTransformStack::PushTransform(
    const mat4& transform) {
  TRACE_DURATION("gfx", "PaperTransformStack::PushTransform", "stack_size",
                 size(), "num_clip_planes", num_clip_planes());

  const auto& cur = Top();

  Item item;
  item.clip_planes.reserve(cur.clip_planes.size());
  for (auto& plane : cur.clip_planes) {
    item.clip_planes.push_back(TransformPlane(transform, plane));
  }
  item.matrix = cur.matrix * transform;

  stack_.push(std::move(item));
  return stack_.top();
}

const PaperTransformStack::Item& PaperTransformStack::PushTranslation(
    const vec3& translation) {
  TRACE_DURATION("gfx", "PaperTransformStack::PushTranslation", "stack_size",
                 size(), "num_clip_planes", num_clip_planes());

  const auto& cur = Top();

  Item item;
  item.clip_planes.reserve(cur.clip_planes.size());
  for (auto& plane : cur.clip_planes) {
    item.clip_planes.push_back(TranslatePlane(translation, plane));
  }
  item.matrix = glm::translate(cur.matrix, translation);

  stack_.push(std::move(item));
  return stack_.top();
}

const PaperTransformStack::Item& PaperTransformStack::PushScale(float scale) {
  TRACE_DURATION("gfx", "PaperTransformStack::PushScale", "stack_size", size(),
                 "num_clip_planes", num_clip_planes());

  const auto& cur = Top();

  Item item;
  item.clip_planes.reserve(cur.clip_planes.size());
  for (auto& plane : cur.clip_planes) {
    item.clip_planes.push_back(ScalePlane(scale, plane));
  }
  item.matrix = glm::scale(cur.matrix, vec3(scale, scale, scale));

  stack_.push(std::move(item));
  return stack_.top();
}

const PaperTransformStack::Item& PaperTransformStack::PushIdentity() {
  stack_.push(Top());
  return stack_.top();
}

const PaperTransformStack::Item& PaperTransformStack::AddClipPlanes(
    const plane3* clip_planes, size_t num_clip_planes) {
  FXL_DCHECK(clip_planes || num_clip_planes == 0);
  if (!clip_planes) {
    return Top();
  } else if (stack_.empty()) {
    PushIdentity();
  }
  auto& cur = stack_.top();
  cur.clip_planes.reserve(cur.clip_planes.size() + num_clip_planes);
  for (size_t i = 0; i < num_clip_planes; ++i) {
    cur.clip_planes.push_back(clip_planes[i]);
  }
  return cur;
}

PaperTransformStack& PaperTransformStack::Pop() {
  if (!empty()) {
    stack_.pop();
  }
  return *this;
}

PaperTransformStack& PaperTransformStack::Clear(
    std::pair<size_t, size_t> stack_size_and_num_clip_planes) {
  auto [target_stack_size, target_num_clip_planes] =
      stack_size_and_num_clip_planes;
  FXL_DCHECK(stack_.size() >= target_stack_size);
  while (stack_.size() > target_stack_size) {
    stack_.pop();
  }
  if (stack_.empty()) {
    FXL_DCHECK(target_num_clip_planes == 0);
  } else {
    auto& clip_planes = stack_.top().clip_planes;
    FXL_DCHECK(target_num_clip_planes <= clip_planes.size())
        << "stack currently has " << clip_planes.size()
        << " clip-planes, which is fewer than the target: "
        << target_num_clip_planes << ".";
    clip_planes.resize(target_num_clip_planes);
  }
  return *this;
}

}  // namespace escher
