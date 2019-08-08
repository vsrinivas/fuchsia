// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/type_shape.h"

#include <algorithm>

namespace fidl {

uint32_t AlignTo(uint64_t size, uint64_t alignment) {
  return static_cast<uint32_t>(
      std::min((size + alignment - 1) & -alignment,
               static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
}

uint32_t ClampedMultiply(uint32_t a, uint32_t b) {
  return static_cast<uint32_t>(
      std::min(static_cast<uint64_t>(a) * static_cast<uint64_t>(b),
               static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
}

uint32_t ClampedAdd(uint32_t a, uint32_t b) {
  return static_cast<uint32_t>(
      std::min(static_cast<uint64_t>(a) + static_cast<uint64_t>(b),
               static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
}

TypeShapeBuilder& TypeShapeBuilder::operator+=(TypeShapeBuilder builder) {
  inline_size += builder.inline_size;
  alignment += builder.alignment;
  recursive.depth += builder.recursive.depth;
  recursive.max_handles += builder.recursive.max_handles;
  recursive.max_out_of_line += builder.recursive.max_out_of_line;
  recursive.has_padding |= builder.recursive.has_padding;
  recursive.has_flexible_envelope |= builder.recursive.has_flexible_envelope;
  return *this;
}

TypeShapeBuilder::Recursive& TypeShapeBuilder::Recursive::AddStructLike(TypeShape typeshape) {
  depth = std::max(depth, typeshape.Depth());
  max_handles = ClampedAdd(max_handles, typeshape.MaxHandles());
  max_out_of_line = ClampedAdd(max_out_of_line, typeshape.MaxOutOfLine());
  has_padding |= typeshape.HasPadding();
  has_flexible_envelope |= typeshape.HasFlexibleEnvelope();
  return *this;
}

TypeShapeBuilder::Recursive& TypeShapeBuilder::Recursive::AddUnionLike(TypeShape typeshape) {
  depth = std::max(depth, typeshape.Depth());
  max_handles = std::max(max_handles, typeshape.MaxHandles());
  max_out_of_line = std::max(max_out_of_line, typeshape.MaxOutOfLine());
  has_padding |= typeshape.HasPadding();
  has_flexible_envelope |= typeshape.HasFlexibleEnvelope();
  return *this;
}

}  // namespace fidl
