// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_TYPE_SHAPE_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_TYPE_SHAPE_H_

#include <cstdint>

// TODO(FIDL-710): We should revisit this namespace choice as part of improving code organization.
namespace fidl {

namespace flat {

struct Object;
struct StructMember;
struct TableMemberUsed;
struct UnionMemberUsed;
struct XUnionMemberUsed;

}  // namespace flat

enum class WireFormat {
  kOld,     // The v0 wire format, where "union" is a static union on-the-wire.
  kV1NoEe,  // The v1-no-ee wire format, where "union" is an extensible union on-the-wire,
            // but without efficient envelope support.
};

struct TypeShape {
  explicit TypeShape(const flat::Object& object, WireFormat wire_format);
  explicit TypeShape(const flat::Object* object, WireFormat wire_format);

  // The inline size of this type, including padding for the type's minimum alignment. For example,
  // "struct S { uint32 a; uint16 b; };" will have an inline_size of 8, not 6: the "packed" size of
  // the struct is 6, but the alignment of its largest member is 4, so 6 is rounded up to 8.
  uint32_t inline_size;

  // The minimum alignment required by this type.
  uint32_t alignment;

  // These values are calculated incorporating both the current TypeShape, and recursively over
  // all child fields. A value of std::numeric_limits<uint32_t>::max() means that the value is
  // potentially unbounded, which can happen for self-recursive aggregate objects. For flexible
  // types, these values is calculated based on the currently-defined members, and does _not_ take
  // potential future members into account.
  uint32_t depth;
  uint32_t max_handles;
  uint32_t max_out_of_line;

  // |has_padding| is true if this type has _either_ inline or out-of-line padding. For flexible
  // types, |has_padding| is calculated based on the currently-defined members, and does _not_ take
  // potential future members into account. (If it did, |has_padding| would have to be true for all
  // flexible types, which doesn't make it very useful.)
  bool has_padding;

  bool has_flexible_envelope;
  // Whether this type transitively contains a union. If this is false, union/xunion transformations
  // can be avoided
  bool contains_union;

  // TODO(fxb/36337): These accessors are for backward compatibility with current code, and could be
  // removed in the future.
  uint32_t InlineSize() const { return inline_size; }
  uint32_t Alignment() const { return alignment; }
  uint32_t Depth() const { return depth; }
  uint32_t MaxHandles() const { return max_handles; }
  uint32_t MaxOutOfLine() const { return max_out_of_line; }
  bool HasPadding() const { return has_padding; }
  bool HasFlexibleEnvelope() const { return has_flexible_envelope; }
  bool ContainsUnion() const { return contains_union; }
};

// |FieldShape| describes the offset and padding information for members that are contained within
// an aggregate type (e.g. struct/union).
// TODO(fxb/36337): We can update |FieldShape| to be a simple offset+padding struct, and remove the
// getter/setter methods since they're purely for backward-compatibility with existing code.
struct FieldShape {
  explicit FieldShape(const flat::StructMember&, const WireFormat wire_format);
  explicit FieldShape(const flat::TableMemberUsed&, const WireFormat wire_format);
  explicit FieldShape(const flat::UnionMemberUsed&, const WireFormat wire_format);
  explicit FieldShape(const flat::XUnionMemberUsed&, const WireFormat wire_format);

  uint32_t Offset() const { return offset; }
  // Padding after this field until the next field or the end of the container.
  // See
  // https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/wire-format/README.md#size-and-alignment
  uint32_t Padding() const { return padding; }

  void SetOffset(uint32_t updated_offset) { offset = updated_offset; }
  void SetPadding(uint32_t updated_padding) { padding = updated_padding; }

  uint32_t offset = 0;
  uint32_t padding = 0;
};

constexpr uint32_t kMessageAlign = 8u;

}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_TYPE_SHAPE_H_
