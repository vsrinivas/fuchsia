// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_TYPE_SHAPE_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_TYPE_SHAPE_H_

#include <cstdint>

// TODO(FIDL-710): We should revisit this namespace choice as part of improving code organization.
namespace fidl {

class TypeShape;

struct TypeShapeBuilder {
  uint32_t inline_size = 0;
  uint32_t alignment = 1;

  // These properties are calculated incorporating both the current TypeShape, and recursively over
  // all child fields. For example, |has_padding| is true if either the current TypeShape has
  // padding, or any child fields themselves have padding.
  struct Recursive {
    uint32_t depth = 0;
    uint32_t max_handles = 0;
    uint32_t max_out_of_line = 0;
    bool has_padding = false;
    bool has_flexible_envelope = false;

    Recursive& AddStructLike(TypeShape typeshape);
    Recursive& AddUnionLike(TypeShape typeshape);
  } recursive = {};

  TypeShapeBuilder& operator+=(TypeShapeBuilder builder);
};

// |TypeShape| describes the wire-format information of a type.
class TypeShape {
 public:
  explicit constexpr TypeShape(TypeShapeBuilder builder)
      : inline_size_(builder.inline_size),
        alignment_(builder.alignment),
        depth_(builder.recursive.depth),
        max_handles_(builder.recursive.max_handles),
        max_out_of_line_(builder.recursive.max_out_of_line),
        has_padding_(builder.recursive.has_padding),
        has_flexible_envelope_(builder.recursive.has_flexible_envelope) {}

  TypeShape() : TypeShape(TypeShapeBuilder{}) {}

  TypeShape(const TypeShape&) = default;
  TypeShape& operator=(const TypeShape&) = default;

  // These properties describe this type only.

  uint32_t InlineSize() const { return inline_size_; }
  uint32_t Alignment() const { return alignment_; }

  // These properties are calculated incorporating both the current TypeShape, and recursively over
  // all child fields.

  uint32_t Depth() const { return depth_; }
  uint32_t MaxHandles() const { return max_handles_; }
  uint32_t MaxOutOfLine() const { return max_out_of_line_; }
  bool HasPadding() const { return has_padding_; }
  bool HasFlexibleEnvelope() const { return has_flexible_envelope_; }

 private:
  uint32_t inline_size_;
  uint32_t alignment_;
  uint32_t depth_;
  uint32_t max_handles_;
  uint32_t max_out_of_line_;
  bool has_padding_;
  bool has_flexible_envelope_;
};

// |FieldShape| describes a |TypeShape| that is embedded in a struct or (x)union as a member field.
// It contains additional offset and padding information.
class FieldShape {
 public:
  // Constructs a |FieldShape| with zero offset and padding.
  // The offset and padding can be updated via |SetOffset| and |SetPadding| respectively.
  explicit FieldShape(TypeShape typeshape) : typeshape_(typeshape), offset_(0), padding_(0) {}

  FieldShape() : FieldShape(TypeShape()) {}

  TypeShape& Typeshape() { return typeshape_; }
  const TypeShape& Typeshape() const { return typeshape_; }

  uint32_t InlineSize() const { return typeshape_.InlineSize(); }
  uint32_t Alignment() const { return typeshape_.Alignment(); }
  uint32_t Depth() const { return typeshape_.Depth(); }
  uint32_t Offset() const { return offset_; }
  // Padding after this field until the next field or the end of the container.
  // See
  // https://fuchsia.googlesource.com/fuchsia/+/master/docs/development/languages/fidl/reference/wire-format/README.md#size-and-alignment
  uint32_t Padding() const { return padding_; }
  uint32_t MaxHandles() const { return typeshape_.MaxHandles(); }
  uint32_t MaxOutOfLine() const { return typeshape_.MaxOutOfLine(); }

  void SetOffset(uint32_t offset) { offset_ = offset; }
  void SetPadding(uint32_t padding) { padding_ = padding; }

 private:
  TypeShape typeshape_;
  uint32_t offset_;
  uint32_t padding_;
};

constexpr uint32_t kMessageAlign = 8u;

uint32_t AlignTo(uint64_t size, uint64_t alignment);

// Multiply |a| and |b| with saturated arithmetic, clamped at UINT32_MAX.
uint32_t ClampedMultiply(uint32_t a, uint32_t b);

// Add |a| and |b| with saturated arithmetic, clamped at UINT32_MAX.
uint32_t ClampedAdd(uint32_t a, uint32_t b);

}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_TYPE_SHAPE_H_
