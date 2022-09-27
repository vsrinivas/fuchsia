// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/type_shape.h"

#include <zircon/assert.h>

#include <algorithm>

#include <safemath/clamped_math.h>

#include "tools/fidl/fidlc/include/fidl/flat/visitor.h"
#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/include/fidl/recursion_detector.h"

namespace {

// TODO(fxbug.dev/7680): We may want to fail instead of saturating.
using DataSize = safemath::ClampedNumeric<uint32_t>;

// Given |offset| in bytes, returns how many padding bytes need to be added to |offset| to be
// aligned to |alignment|.
DataSize Padding(const DataSize offset, const DataSize alignment) {
  // See <https://en.wikipedia.org/wiki/Data_structure_alignment#Computing_padding> for a context on
  // computing the amount of padding required.

  // The following expression is from <https://stackoverflow.com/a/32104582> and is equivalent to
  // "(alignment - (offset % alignment)) % alignment".
  return (~offset.RawValue() + 1) & (alignment.RawValue() - 1);
}

// Given |size| and |alignment| in bytes, returns |size| "rounded up" to the next |alignment|
// interval.
DataSize AlignTo(uint32_t size, uint64_t alignment) {
  // From <https://en.wikipedia.org/wiki/Data_structure_alignment#Computing_padding>.
  return (size + (alignment - 1)) & -alignment;
}

// Given |size|, returns |size| "rounded up" to the next alignment interval required by an
// out-of-line FIDL object.
DataSize ObjectAlign(uint32_t size) { return AlignTo(size, 8); }

}  // namespace

namespace std {

// Add a partial specialization for std::numeric_limits<DataSize>::max(), which would
// otherwise return 0 (see
// <https://stackoverflow.com/questions/35575276/why-does-stdnumeric-limitssecondsmax-return-0> if
// you're curious about why.)
template <>
struct numeric_limits<DataSize> {
  static constexpr DataSize max() noexcept { return DataSize(numeric_limits<uint32_t>::max()); }
};

static_assert(numeric_limits<DataSize>::max() == numeric_limits<uint32_t>::max());

}  // namespace std

namespace {

constexpr uint32_t kHandleSize = 4;

namespace flat = fidl::flat;
namespace types = fidl::types;
using WireFormat = fidl::WireFormat;

DataSize UnalignedSize(const flat::Object& object, WireFormat wire_format);
DataSize UnalignedSize(const flat::Object* object, WireFormat wire_format);
[[maybe_unused]] DataSize Alignment(const flat::Object& object, WireFormat wire_format);
[[maybe_unused]] DataSize Alignment(const flat::Object* object, WireFormat wire_format);
DataSize Depth(const flat::Object& object, WireFormat wire_format);
[[maybe_unused]] DataSize Depth(const flat::Object* object, WireFormat wire_format);
DataSize MaxHandles(const flat::Object& object);
[[maybe_unused]] DataSize MaxHandles(const flat::Object* object);
DataSize MaxOutOfLine(const flat::Object& object, WireFormat wire_format);
[[maybe_unused]] DataSize MaxOutOfLine(const flat::Object* object, WireFormat wire_format);
bool HasPadding(const flat::Object& object, WireFormat wire_format);
[[maybe_unused]] bool HasPadding(const flat::Object* object, WireFormat wire_format);
bool HasEnvelope(const flat::Object& object, WireFormat wire_format);
[[maybe_unused]] bool HasEnvelope(const flat::Object* object, WireFormat wire_format);
bool HasFlexibleEnvelope(const flat::Object& object, WireFormat wire_format);
[[maybe_unused]] bool HasFlexibleEnvelope(const flat::Object* object, WireFormat wire_format);

DataSize AlignedSize(const flat::Object& object, WireFormat wire_format) {
  return AlignTo(UnalignedSize(object, wire_format), Alignment(object, wire_format));
}

[[maybe_unused]] DataSize AlignedSize(const flat::Object* object, WireFormat wire_format) {
  return AlignedSize(*object, wire_format);
}

template <typename T>
class TypeShapeVisitor : public flat::Object::Visitor<T> {
 public:
  TypeShapeVisitor() = delete;
  explicit TypeShapeVisitor(WireFormat wire_format) : wire_format_(wire_format) {}

 protected:
  WireFormat wire_format() const { return wire_format_; }

 private:
  const WireFormat wire_format_;
};

class UnalignedSizeVisitor final : public TypeShapeVisitor<DataSize> {
 public:
  using TypeShapeVisitor<DataSize>::TypeShapeVisitor;

  std::any Visit(const flat::ArrayType& object) override {
    return UnalignedSize(object.element_type) * object.element_count->value;
  }

  std::any Visit(const flat::VectorType& object) override { return DataSize(16); }

  std::any Visit(const flat::StringType& object) override { return DataSize(16); }

  std::any Visit(const flat::HandleType& object) override { return DataSize(kHandleSize); }

  std::any Visit(const flat::PrimitiveType& object) override {
    switch (object.subtype) {
      case types::PrimitiveSubtype::kBool:
      case types::PrimitiveSubtype::kInt8:
      case types::PrimitiveSubtype::kUint8:
      case types::PrimitiveSubtype::kZxUchar:
        return DataSize(1);
      case types::PrimitiveSubtype::kInt16:
      case types::PrimitiveSubtype::kUint16:
        return DataSize(2);
      case types::PrimitiveSubtype::kInt32:
      case types::PrimitiveSubtype::kUint32:
      case types::PrimitiveSubtype::kFloat32:
        return DataSize(4);
      case types::PrimitiveSubtype::kInt64:
      case types::PrimitiveSubtype::kUint64:
      case types::PrimitiveSubtype::kZxUsize:
      case types::PrimitiveSubtype::kZxUintptr:
      case types::PrimitiveSubtype::kFloat64:
        return DataSize(8);
    }
  }

  std::any Visit(const flat::InternalType& object) override {
    switch (object.subtype) {
      case fidl::types::InternalSubtype::kTransportErr:
        return DataSize(4);
    }
  }

  std::any Visit(const flat::IdentifierType& object) override {
    switch (object.nullability) {
      case types::Nullability::kNullable:
        switch (object.type_decl->kind) {
          case flat::Decl::Kind::kProtocol:
          case flat::Decl::Kind::kService:
            return DataSize(kHandleSize);
          // TODO(fxbug.dev/70186): this should be handled as a box and nullable structs should
          // never be visited
          case flat::Decl::Kind::kStruct:
            return DataSize(8);
          case flat::Decl::Kind::kUnion:
            switch (wire_format()) {
              case WireFormat::kV1NoEe:
                return DataSize(24);
              case WireFormat::kV2:
                return DataSize(16);
            }
          case flat::Decl::Kind::kBits:
          case flat::Decl::Kind::kBuiltin:
          case flat::Decl::Kind::kConst:
          case flat::Decl::Kind::kEnum:
          case flat::Decl::Kind::kNewType:
          case flat::Decl::Kind::kResource:
          case flat::Decl::Kind::kTable:
          case flat::Decl::Kind::kTypeAlias:
            ZX_PANIC("UnalignedSize(flat::IdentifierType&) called on invalid nullable kind");
        }
      case types::Nullability::kNonnullable: {
        return UnalignedSize(object.type_decl);
      }
    }
  }

  std::any Visit(const flat::BoxType& object) override { return DataSize(8); }

  std::any Visit(const flat::TransportSideType& object) override { return DataSize(kHandleSize); }

  std::any Visit(const flat::Enum& object) override {
    return UnalignedSize(object.subtype_ctor->type);
  }

  std::any Visit(const flat::Bits& object) override {
    return UnalignedSize(object.subtype_ctor->type);
  }

  std::any Visit(const flat::Service& object) override { return DataSize(kHandleSize); }

  std::any Visit(const flat::NewType& object) override {
    return UnalignedSize(object.type_ctor->type);
  }

  std::any Visit(const flat::Struct& object) override {
    DataSize size = 0;
    if (object.members.empty()) {
      return DataSize(1 + size);
    }

    for (const auto& member : object.members) {
      const DataSize member_size = UnalignedSize(member) + member.fieldshape(wire_format()).padding;
      size += member_size;
    }

    return size;
  }

  std::any Visit(const flat::Struct::Member& object) override {
    return UnalignedSize(object.type_ctor->type);
  }

  std::any Visit(const flat::Table& object) override { return DataSize(16); }

  std::any Visit(const flat::Table::Member& object) override {
    return object.maybe_used ? UnalignedSize(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::Table::Member::Used& object) override {
    return UnalignedSize(object.type_ctor->type);
  }

  std::any Visit(const flat::Union& object) override {
    switch (wire_format()) {
      case WireFormat::kV1NoEe:
        return DataSize(24);
      case WireFormat::kV2:
        return DataSize(16);
    }
  }

  std::any Visit(const flat::Union::Member& object) override {
    return object.maybe_used ? UnalignedSize(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::Union::Member::Used& object) override {
    return UnalignedSize(object.type_ctor->type);
  }

  std::any Visit(const flat::Protocol& object) override { return DataSize(kHandleSize); }

 private:
  DataSize UnalignedSize(const flat::Object& object) { return object.Accept(this); }

  DataSize UnalignedSize(const flat::Object* object) { return UnalignedSize(*object); }
};

class AlignmentVisitor final : public TypeShapeVisitor<DataSize> {
 public:
  using TypeShapeVisitor<DataSize>::TypeShapeVisitor;

  std::any Visit(const flat::ArrayType& object) override { return Alignment(object.element_type); }

  std::any Visit(const flat::VectorType& object) override { return DataSize(8); }

  std::any Visit(const flat::StringType& object) override { return DataSize(8); }

  std::any Visit(const flat::HandleType& object) override { return DataSize(kHandleSize); }

  std::any Visit(const flat::PrimitiveType& object) override {
    return UnalignedSize(object, wire_format());
  }

  std::any Visit(const flat::InternalType& object) override {
    switch (object.subtype) {
      case fidl::types::InternalSubtype::kTransportErr:
        return DataSize(4);
    }
  }

  std::any Visit(const flat::IdentifierType& object) override {
    switch (object.nullability) {
      case types::Nullability::kNullable:
        switch (object.type_decl->kind) {
          case flat::Decl::Kind::kProtocol:
          case flat::Decl::Kind::kService:
            return DataSize(kHandleSize);
          // TODO(fxbug.dev/70186): this should be handled as a box and nullable structs should
          // never be visited
          case flat::Decl::Kind::kStruct:
          case flat::Decl::Kind::kUnion:
            return DataSize(8);
          case flat::Decl::Kind::kBits:
          case flat::Decl::Kind::kBuiltin:
          case flat::Decl::Kind::kConst:
          case flat::Decl::Kind::kEnum:
          case flat::Decl::Kind::kNewType:
          case flat::Decl::Kind::kResource:
          case flat::Decl::Kind::kTable:
          case flat::Decl::Kind::kTypeAlias:
            ZX_PANIC("Alignment(flat::IdentifierType&) called on invalid nullable kind");
        }
      case types::Nullability::kNonnullable:
        return Alignment(object.type_decl);
    }
  }

  std::any Visit(const flat::BoxType& object) override { return DataSize(8); }

  std::any Visit(const flat::TransportSideType& object) override { return DataSize(kHandleSize); }

  std::any Visit(const flat::Enum& object) override { return Alignment(object.subtype_ctor->type); }

  std::any Visit(const flat::Bits& object) override { return Alignment(object.subtype_ctor->type); }

  std::any Visit(const flat::Service& object) override { return DataSize(kHandleSize); }

  std::any Visit(const flat::NewType& object) override { return Alignment(object.type_ctor->type); }

  std::any Visit(const flat::Struct& object) override {
    if (object.recursive) {
      // |object| is recursive, therefore there must be a pointer to this struct in the recursion
      // chain, with pointer-sized alignment.
      return DataSize(8);
    }

    if (object.members.empty()) {
      // Empty struct.

      return DataSize(1);
    }

    DataSize alignment = 0;

    for (const auto& member : object.members) {
      alignment = std::max(alignment, Alignment(member));
    }

    return alignment;
  }

  std::any Visit(const flat::Struct::Member& object) override {
    return Alignment(object.type_ctor->type);
  }

  std::any Visit(const flat::Table& object) override { return DataSize(8); }

  std::any Visit(const flat::Table::Member& object) override {
    return object.maybe_used ? Alignment(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::Table::Member::Used& object) override {
    return Alignment(object.type_ctor->type);
  }

  std::any Visit(const flat::Union& object) override { return DataSize(8); }

  std::any Visit(const flat::Union::Member& object) override {
    return object.maybe_used ? Alignment(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::UnionMember::Used& object) override {
    return Alignment(object.type_ctor->type);
  }

  std::any Visit(const flat::Protocol& object) override { return DataSize(kHandleSize); }

 private:
  DataSize Alignment(const flat::Object& object) { return object.Accept(this); }

  DataSize Alignment(const flat::Object* object) { return Alignment(*object); }
};

class DepthVisitor : public TypeShapeVisitor<DataSize> {
 public:
  using TypeShapeVisitor<DataSize>::TypeShapeVisitor;

  std::any Visit(const flat::ArrayType& object) override { return Depth(object.element_type); }

  std::any Visit(const flat::VectorType& object) override {
    return DataSize(1) + Depth(object.element_type);
  }

  std::any Visit(const flat::StringType& object) override { return DataSize(1); }

  std::any Visit(const flat::HandleType& object) override { return DataSize(0); }

  std::any Visit(const flat::PrimitiveType& object) override { return DataSize(0); }

  std::any Visit(const flat::InternalType& object) override {
    switch (object.subtype) {
      case fidl::types::InternalSubtype::kTransportErr:
        return DataSize(0);
    }
  }

  std::any Visit(const flat::IdentifierType& object) override {
    thread_local RecursionDetector recursion_detector;

    auto guard = recursion_detector.Enter(&object);
    if (!guard) {
      return DataSize(0);
    }

    switch (object.nullability) {
      case types::Nullability::kNullable:
        switch (object.type_decl->kind) {
          case flat::Decl::Kind::kProtocol:
          case flat::Decl::Kind::kService:
            return DataSize(0);
          case flat::Decl::Kind::kStruct:
            return DataSize(1) + Depth(object.type_decl);
          case flat::Decl::Kind::kUnion:
            return Depth(object.type_decl);
          case flat::Decl::Kind::kBits:
          case flat::Decl::Kind::kBuiltin:
          case flat::Decl::Kind::kConst:
          case flat::Decl::Kind::kEnum:
          case flat::Decl::Kind::kNewType:
          case flat::Decl::Kind::kResource:
          case flat::Decl::Kind::kTable:
          case flat::Decl::Kind::kTypeAlias:
            ZX_PANIC("Depth(flat::IdentifierType&) called on invalid nullable kind");
        }
      case types::Nullability::kNonnullable:
        switch (object.type_decl->kind) {
          case flat::Decl::Kind::kBits:
          case flat::Decl::Kind::kConst:
          case flat::Decl::Kind::kEnum:
          case flat::Decl::Kind::kProtocol:
          case flat::Decl::Kind::kResource:
          case flat::Decl::Kind::kService:
            return DataSize(0);
          case flat::Decl::Kind::kNewType:
          case flat::Decl::Kind::kStruct:
          case flat::Decl::Kind::kTable:
          case flat::Decl::Kind::kTypeAlias:
          case flat::Decl::Kind::kUnion:
            return Depth(object.type_decl);
          case flat::Decl::Kind::kBuiltin:
            ZX_PANIC("unexpected builtin");
        }
      default:
        ZX_PANIC("unexpected nullability type");
    }
  }

  std::any Visit(const flat::NewType& object) override { return Depth(object.type_ctor->type); }

  std::any Visit(const flat::BoxType& object) override {
    // The nullable struct case will add one, no need to do it here.
    return Depth(object.boxed_type);
  }

  std::any Visit(const flat::TransportSideType& object) override { return DataSize(0); }

  std::any Visit(const flat::Enum& object) override { return Depth(object.subtype_ctor->type); }

  std::any Visit(const flat::Bits& object) override { return Depth(object.subtype_ctor->type); }

  std::any Visit(const flat::Service& object) override { return DataSize(0); }

  std::any Visit(const flat::Struct& object) override {
    if (object.recursive) {
      return std::numeric_limits<DataSize>::max();
    }

    DataSize max_depth = 0;

    for (const auto& member : object.members) {
      max_depth = std::max(max_depth, Depth(member));
    }

    return max_depth;
  }

  std::any Visit(const flat::Struct::Member& object) override {
    return Depth(object.type_ctor->type);
  }

  std::any Visit(const flat::Table& object) override {
    DataSize max_depth = 0;

    for (const auto& member : object.members) {
      max_depth = std::max(max_depth, Depth(member));
    }

    return DataSize(1) + max_depth;
  }

  std::any Visit(const flat::Table::Member& object) override {
    return object.maybe_used ? Depth(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::Table::Member::Used& object) override {
    return DataSize(1) + Depth(object.type_ctor->type);
  }

  std::any Visit(const flat::Union& object) override {
    DataSize max_depth;

    for (const auto& member : object.members) {
      max_depth = std::max(max_depth, Depth(member));
    }

    return DataSize(1) + max_depth;
  }

  std::any Visit(const flat::Union::Member& object) override {
    return object.maybe_used ? Depth(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::Union::Member::Used& object) override {
    return Depth(object.type_ctor->type);
  }

  std::any Visit(const flat::Protocol& object) override { return DataSize(0); }

 protected:
  DataSize Depth(const flat::Object& object) { return object.Accept(this); }

  DataSize Depth(const flat::Object* object) { return Depth(*object); }
};

// This visitor calculates depth according to the "old" wire format (i.e. with
// static unions). It leverages |DepthVisitor| for any cases that are wire format
// dependent, and overrides cases that are different in the old wire format (i.e.
// unions).
class OldWireFormatDepthVisitor final : public DepthVisitor {
 public:
  // A wire format is provided here because the default constructor is disabled. In actuality,
  // the wire format does not matter as this class is hardcoded to return depth under the
  // "old" wire format.
  explicit OldWireFormatDepthVisitor(WireFormat wire_format) : DepthVisitor(wire_format) {}

  // A nullable static union introduces an extra level of depth, since it gets replaced with
  // a presence pointer.
  std::any Visit(const flat::IdentifierType& object) override {
    if (object.nullability == types::Nullability::kNullable &&
        object.type_decl->kind == flat::Decl::Kind::kUnion) {
      return DataSize(1) + Depth(object.type_decl);
    }

    return DepthVisitor::Visit(object);
  }

  // Static unions do not introduce an extra level of depth because they hold data inline,
  // without the use of an envelope
  std::any Visit(const flat::Union& object) override {
    DataSize max_depth;

    for (const auto& member : object.members) {
      max_depth = std::max(max_depth, Depth(member));
    }

    return max_depth;
  }
};

class MaxHandlesVisitor final : public flat::Object::Visitor<DataSize> {
 public:
  std::any Visit(const flat::ArrayType& object) override {
    return MaxHandles(object.element_type) * object.element_count->value;
  }

  std::any Visit(const flat::VectorType& object) override {
    return MaxHandles(object.element_type) * object.element_count->value;
  }

  std::any Visit(const flat::StringType& object) override { return DataSize(0); }

  std::any Visit(const flat::HandleType& object) override { return DataSize(1); }

  std::any Visit(const flat::PrimitiveType& object) override { return DataSize(0); }

  std::any Visit(const flat::InternalType& object) override {
    switch (object.subtype) {
      case fidl::types::InternalSubtype::kTransportErr:
        return DataSize(0);
    }
  }

  std::any Visit(const flat::IdentifierType& object) override {
    thread_local RecursionDetector recursion_detector;

    // TODO(fxbug.dev/36327): This code is technically incorrect; see the visit(Struct&) overload
    // for more details.
    auto guard = recursion_detector.Enter(&object);
    if (!guard) {
      return DataSize(0);
    }

    return MaxHandles(object.type_decl);
  }

  std::any Visit(const flat::BoxType& object) override { return MaxHandles(object.boxed_type); }

  std::any Visit(const flat::TransportSideType& object) override { return DataSize(1); }

  std::any Visit(const flat::Enum& object) override {
    return MaxHandles(object.subtype_ctor->type);
  }

  std::any Visit(const flat::Bits& object) override {
    return MaxHandles(object.subtype_ctor->type);
  }

  std::any Visit(const flat::Service& object) override { return DataSize(1); }

  std::any Visit(const flat::Struct& object) override {
    // TODO(fxbug.dev/36327): This is technically incorrect: if a struct is recursive, it may not
    // directly contain a handle, but could contain e.g. a struct that contains a handle. In that
    // case, this code will return 0 instead of std::numeric_limits<DataSize>::max(). This does pass
    // all current tests and Fuchsia compilation, so fixing it isn't super-urgent.
    if (object.recursive) {
      for (const auto& member : object.members) {
        switch (member.type_ctor->type->kind) {
          case flat::Type::Kind::kHandle:
          case flat::Type::Kind::kTransportSide:
            return std::numeric_limits<DataSize>::max();
          case flat::Type::Kind::kArray:
          case flat::Type::Kind::kVector:
          case flat::Type::Kind::kString:
          case flat::Type::Kind::kPrimitive:
          case flat::Type::Kind::kInternal:
          case flat::Type::Kind::kIdentifier:
          case flat::Type::Kind::kBox:
            continue;
          case flat::Type::Kind::kUntypedNumeric:
            ZX_PANIC("should not have untyped numeric here");
        }
      }

      return DataSize(0);
    }

    DataSize max_handles = 0;

    for (const auto& member : object.members) {
      max_handles += MaxHandles(member);
    }

    return max_handles;
  }

  std::any Visit(const flat::NewType& object) override {
    return MaxHandles(object.type_ctor->type);
  }

  std::any Visit(const flat::Struct::Member& object) override {
    return MaxHandles(object.type_ctor->type);
  }

  std::any Visit(const flat::Table& object) override {
    DataSize max_handles = 0;

    for (const auto& member : object.members) {
      max_handles += MaxHandles(member);
    }

    return max_handles;
  }

  std::any Visit(const flat::Table::Member& object) override {
    return object.maybe_used ? MaxHandles(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::Table::Member::Used& object) override {
    return MaxHandles(object.type_ctor->type);
  }

  std::any Visit(const flat::Union& object) override {
    DataSize max_handles;

    for (const auto& member : object.members) {
      max_handles = std::max(max_handles, MaxHandles(member));
    }

    return max_handles;
  }

  std::any Visit(const flat::Union::Member& object) override {
    return object.maybe_used ? MaxHandles(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::Union::Member::Used& object) override {
    return MaxHandles(object.type_ctor->type);
  }

  std::any Visit(const flat::Protocol& object) override { return DataSize(1); }
};

class MaxOutOfLineVisitor final : public TypeShapeVisitor<DataSize> {
 public:
  using TypeShapeVisitor<DataSize>::TypeShapeVisitor;

  std::any Visit(const flat::ArrayType& object) override {
    return MaxOutOfLine(object.element_type) * DataSize(object.element_count->value);
  }

  std::any Visit(const flat::VectorType& object) override {
    return ObjectAlign(UnalignedSize(object.element_type, wire_format()) *
                       object.element_count->value) +
           ObjectAlign(MaxOutOfLine(object.element_type)) * object.element_count->value;
  }

  std::any Visit(const flat::StringType& object) override {
    return object.max_size ? ObjectAlign(object.max_size->value)
                           : std::numeric_limits<DataSize>::max();
  }

  std::any Visit(const flat::HandleType& object) override { return DataSize(0); }

  std::any Visit(const flat::PrimitiveType& object) override { return DataSize(0); }

  std::any Visit(const flat::InternalType& object) override {
    switch (object.subtype) {
      case fidl::types::InternalSubtype::kTransportErr:
        return DataSize(0);
    }
  }

  std::any Visit(const flat::IdentifierType& object) override {
    if (object.type_decl->recursive) {
      return std::numeric_limits<DataSize>::max();
    }

    switch (object.nullability) {
      case types::Nullability::kNullable: {
        switch (object.type_decl->kind) {
          case flat::Decl::Kind::kProtocol:
          case flat::Decl::Kind::kService:
            return DataSize(0);
          case flat::Decl::Kind::kStruct:
            return ObjectAlign(UnalignedSize(object.type_decl, wire_format())) +
                   MaxOutOfLine(object.type_decl);
          case flat::Decl::Kind::kUnion:
            return MaxOutOfLine(object.type_decl);
          case flat::Decl::Kind::kBits:
          case flat::Decl::Kind::kBuiltin:
          case flat::Decl::Kind::kConst:
          case flat::Decl::Kind::kEnum:
          case flat::Decl::Kind::kNewType:
          case flat::Decl::Kind::kResource:
          case flat::Decl::Kind::kTable:
          case flat::Decl::Kind::kTypeAlias:
            ZX_PANIC("MaxOutOfLine(flat::IdentifierType&) called on invalid nullable kind");
        }
      }
      case types::Nullability::kNonnullable:
        return MaxOutOfLine(object.type_decl);
    }
  }

  std::any Visit(const flat::NewType& object) override {
    return MaxOutOfLine(object.type_ctor->type);
  }

  std::any Visit(const flat::BoxType& object) override { return MaxOutOfLine(object.boxed_type); }

  std::any Visit(const flat::TransportSideType& object) override { return DataSize(0); }

  std::any Visit(const flat::Enum& object) override {
    return MaxOutOfLine(object.subtype_ctor->type);
  }

  std::any Visit(const flat::Bits& object) override {
    return MaxOutOfLine(object.subtype_ctor->type);
  }

  std::any Visit(const flat::Service& object) override { return DataSize(0); }

  std::any Visit(const flat::Struct& object) override {
    DataSize max_out_of_line = 0;

    for (const auto& member : object.members) {
      max_out_of_line += MaxOutOfLine(member);
    }

    return max_out_of_line;
  }

  std::any Visit(const flat::Struct::Member& object) override {
    return MaxOutOfLine(object.type_ctor->type);
  }

  std::any Visit(const flat::Table& object) override {
    DataSize max_out_of_line = 0;

    for (const auto& member : object.members) {
      if (wire_format() == WireFormat::kV2) {
        if (UnalignedSize(member, wire_format()) <= 4) {
          continue;
        }
      }
      max_out_of_line += ObjectAlign(UnalignedSize(member, wire_format())) + MaxOutOfLine(member);
    }

    // The maximum number of envelopes is determined by the maximum _unreserved_ ordinal.
    // Any trailing reserved ordinals MUST NOT be present in the array of envelopes.
    // For example, a table that looks like
    // "table T { 1: int32 i; 2: reserved; 3: uint32 u; 4: reserved; }"
    // has an envelope array size of 3, not 4.
    ZX_ASSERT(object.members.size() <= INT32_MAX);
    int max_unreserved_index = -1;
    for (int i = static_cast<int>(object.members.size()) - 1; i >= 0; i--) {
      if (object.members.at(i).maybe_used) {
        max_unreserved_index = i;
        break;
      }
    }

    const size_t envelope_array_size = max_unreserved_index == -1 ? 0 : max_unreserved_index + 1;

    DataSize envelope_size = 0;
    switch (wire_format()) {
      case WireFormat::kV1NoEe:
        envelope_size = 16;
        break;
      case WireFormat::kV2:
        envelope_size = 8;
        break;
    }
    return DataSize(envelope_array_size) * envelope_size + max_out_of_line;
  }

  std::any Visit(const flat::Table::Member& object) override {
    return object.maybe_used ? MaxOutOfLine(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::Table::Member::Used& object) override {
    return ObjectAlign(MaxOutOfLine(object.type_ctor->type));
  }

  std::any Visit(const flat::Union& object) override {
    DataSize max_out_of_line = 0;

    for (const auto& member : object.members) {
      if (wire_format() == WireFormat::kV2) {
        if (UnalignedSize(member, wire_format()) <= 4) {
          continue;
        }
      }
      max_out_of_line =
          std::max(max_out_of_line,
                   ObjectAlign(UnalignedSize(member, wire_format())) + MaxOutOfLine(member));
    }

    return max_out_of_line;
  }

  std::any Visit(const flat::Union::Member& object) override {
    return object.maybe_used ? MaxOutOfLine(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::Union::Member::Used& object) override {
    return MaxOutOfLine(object.type_ctor->type);
  }

  std::any Visit(const flat::Protocol& object) override { return DataSize(0); }

 private:
  DataSize MaxOutOfLine(const flat::Object& object) { return object.Accept(this); }

  DataSize MaxOutOfLine(const flat::Object* object) { return MaxOutOfLine(*object); }
};

class HasPaddingVisitor final : public TypeShapeVisitor<bool> {
 public:
  using TypeShapeVisitor<bool>::TypeShapeVisitor;

  std::any Visit(const flat::ArrayType& object) override { return HasPadding(object.element_type); }

  std::any Visit(const flat::VectorType& object) override {
    auto element_has_innate_padding = [&] { return HasPadding(object.element_type); };

    auto element_has_trailing_padding = [&] {
      // A vector will always have padding out-of-line for its contents unless its element_type's
      // natural size is a multiple of 8.
      return Padding(UnalignedSize(object.element_type, wire_format()), 8) != 0;
    };

    return element_has_trailing_padding() || element_has_innate_padding();
  }

  std::any Visit(const flat::StringType& object) override { return true; }

  std::any Visit(const flat::HandleType& object) override { return false; }

  std::any Visit(const flat::PrimitiveType& object) override { return false; }

  std::any Visit(const flat::InternalType& object) override {
    switch (object.subtype) {
      case fidl::types::InternalSubtype::kTransportErr:
        return false;
    }
  }

  std::any Visit(const flat::IdentifierType& object) override {
    thread_local RecursionDetector recursion_detector;

    auto guard = recursion_detector.Enter(&object);
    if (!guard) {
      return false;
    }

    switch (object.nullability) {
      case types::Nullability::kNullable:
        switch (object.type_decl->kind) {
          case flat::Decl::Kind::kProtocol:
          case flat::Decl::Kind::kService:
            return false;
          // TODO(fxbug.dev/70186): this should be handled as a box and nullable structs should
          // never be visited
          case flat::Decl::Kind::kStruct:
          case flat::Decl::Kind::kUnion:
            return Padding(UnalignedSize(object.type_decl, wire_format()), 8) > 0 ||
                   HasPadding(object.type_decl);
          case flat::Decl::Kind::kBits:
          case flat::Decl::Kind::kBuiltin:
          case flat::Decl::Kind::kConst:
          case flat::Decl::Kind::kEnum:
          case flat::Decl::Kind::kNewType:
          case flat::Decl::Kind::kResource:
          case flat::Decl::Kind::kTable:
          case flat::Decl::Kind::kTypeAlias:
            ZX_PANIC("HasPadding(flat::IdentifierType&) called on invalid nullable kind");
        }
      case types::Nullability::kNonnullable:
        return HasPadding(object.type_decl);
    }
  }

  std::any Visit(const flat::NewType& object) override {
    return HasPadding(object.type_ctor->type);
  }

  std::any Visit(const flat::BoxType& object) override { return HasPadding(object.boxed_type); }

  std::any Visit(const flat::TransportSideType& object) override { return false; }

  std::any Visit(const flat::Enum& object) override {
    return HasPadding(object.subtype_ctor->type);
  }

  std::any Visit(const flat::Bits& object) override {
    return HasPadding(object.subtype_ctor->type);
  }

  std::any Visit(const flat::Service& object) override { return false; }

  std::any Visit(const flat::Struct& object) override {
    for (const auto& member : object.members) {
      if (HasPadding(member)) {
        return true;
      }
    }

    return false;
  }

  std::any Visit(const flat::Struct::Member& object) override {
    return object.fieldshape(wire_format()).padding > 0 || HasPadding(object.type_ctor->type);
  }

  std::any Visit(const flat::Table& object) override {
    for (const auto& member : object.members) {
      if (HasPadding(member)) {
        return true;
      }
    }

    return false;
  }

  std::any Visit(const flat::Table::Member& object) override {
    return object.maybe_used ? HasPadding(*object.maybe_used) : false;
  }

  std::any Visit(const flat::Table::Member::Used& object) override {
    return Padding(UnalignedSize(object.type_ctor->type, wire_format()), 8) > 0 ||
           HasPadding(object.type_ctor->type) || object.fieldshape(wire_format()).padding > 0;
  }

  std::any Visit(const flat::Union& object) override {
    // TODO(fxbug.dev/36332): Unions currently return true for has_padding in all cases, which
    // should be fixed.
    return true;
  }

  std::any Visit(const flat::Union::Member& object) override {
    return object.maybe_used ? HasPadding(*object.maybe_used) : false;
  }

  std::any Visit(const flat::Union::Member::Used& object) override {
    // TODO(fxbug.dev/36331): This code only accounts for inline padding for the union member. We
    // also need to account for out-of-line padding.
    return object.fieldshape(wire_format()).padding > 0;
  }

  std::any Visit(const flat::Protocol& object) override { return false; }

 private:
  bool HasPadding(const flat::Object& object) { return object.Accept(this); }

  bool HasPadding(const flat::Object* object) { return HasPadding(*object); }
};

class HasEnvelopeVisitor final : public TypeShapeVisitor<bool> {
 public:
  using TypeShapeVisitor<bool>::TypeShapeVisitor;

  std::any Visit(const flat::ArrayType& object) override {
    return HasEnvelope(object.element_type, wire_format());
  }

  std::any Visit(const flat::VectorType& object) override {
    return HasEnvelope(object.element_type, wire_format());
  }

  std::any Visit(const flat::StringType& object) override { return false; }

  std::any Visit(const flat::HandleType& object) override { return false; }

  std::any Visit(const flat::PrimitiveType& object) override { return false; }

  std::any Visit(const flat::InternalType& object) override {
    switch (object.subtype) {
      case fidl::types::InternalSubtype::kTransportErr:
        return false;
    }
  }

  std::any Visit(const flat::IdentifierType& object) override {
    thread_local RecursionDetector recursion_detector;

    auto guard = recursion_detector.Enter(&object);
    if (!guard) {
      return false;
    }

    return HasEnvelope(object.type_decl, wire_format());
  }

  std::any Visit(const flat::NewType& object) override {
    return HasEnvelope(object.type_ctor->type, wire_format());
  }

  std::any Visit(const flat::BoxType& object) override {
    return HasEnvelope(object.boxed_type, wire_format());
  }

  std::any Visit(const flat::TransportSideType& object) override { return false; }

  std::any Visit(const flat::Enum& object) override { return false; }

  std::any Visit(const flat::Bits& object) override { return false; }

  std::any Visit(const flat::Service& object) override { return false; }

  std::any Visit(const flat::Struct& object) override {
    for (const auto& member : object.members) {
      if (HasEnvelope(member, wire_format())) {
        return true;
      }
    }

    return false;
  }

  std::any Visit(const flat::Struct::Member& object) override {
    return HasEnvelope(object.type_ctor->type, wire_format());
  }

  std::any Visit(const flat::Table& object) override { return true; }

  std::any Visit(const flat::Table::Member& object) override { return true; }

  std::any Visit(const flat::Table::Member::Used& object) override { return true; }

  std::any Visit(const flat::Union& object) override { return true; }

  std::any Visit(const flat::Union::Member& object) override { return true; }

  std::any Visit(const flat::Union::Member::Used& object) override { return true; }

  std::any Visit(const flat::Protocol& object) override { return false; }
};

class HasFlexibleEnvelopeVisitor final : public TypeShapeVisitor<bool> {
 public:
  using TypeShapeVisitor<bool>::TypeShapeVisitor;

  std::any Visit(const flat::ArrayType& object) override {
    return HasFlexibleEnvelope(object.element_type, wire_format());
  }

  std::any Visit(const flat::VectorType& object) override {
    return HasFlexibleEnvelope(object.element_type, wire_format());
  }

  std::any Visit(const flat::StringType& object) override { return false; }

  std::any Visit(const flat::HandleType& object) override { return false; }

  std::any Visit(const flat::PrimitiveType& object) override { return false; }

  std::any Visit(const flat::InternalType& object) override {
    switch (object.subtype) {
      case fidl::types::InternalSubtype::kTransportErr:
        return false;
    }
  }

  std::any Visit(const flat::IdentifierType& object) override {
    thread_local RecursionDetector recursion_detector;

    auto guard = recursion_detector.Enter(&object);
    if (!guard) {
      return false;
    }

    return HasFlexibleEnvelope(object.type_decl, wire_format());
  }

  std::any Visit(const flat::NewType& object) override {
    return HasFlexibleEnvelope(object.type_ctor->type, wire_format());
  }

  std::any Visit(const flat::BoxType& object) override {
    return HasFlexibleEnvelope(object.boxed_type, wire_format());
  }

  std::any Visit(const flat::TransportSideType& object) override { return false; }

  std::any Visit(const flat::Enum& object) override {
    return HasFlexibleEnvelope(object.subtype_ctor->type, wire_format());
  }

  std::any Visit(const flat::Bits& object) override {
    return HasFlexibleEnvelope(object.subtype_ctor->type, wire_format());
  }

  std::any Visit(const flat::Service& object) override { return false; }

  std::any Visit(const flat::Struct& object) override {
    for (const auto& member : object.members) {
      if (HasFlexibleEnvelope(member, wire_format())) {
        return true;
      }
    }

    return false;
  }

  std::any Visit(const flat::Struct::Member& object) override {
    return HasFlexibleEnvelope(object.type_ctor->type, wire_format());
  }

  std::any Visit(const flat::Table& object) override {
    if (object.strictness == types::Strictness::kFlexible) {
      return true;
    }

    for (const auto& member : object.members) {
      if (HasFlexibleEnvelope(member, wire_format())) {
        return true;
      }
    }

    return false;
  }

  std::any Visit(const flat::Table::Member& object) override {
    return object.maybe_used ? HasFlexibleEnvelope(*object.maybe_used, wire_format()) : false;
  }

  std::any Visit(const flat::Table::Member::Used& object) override {
    return HasFlexibleEnvelope(object.type_ctor->type, wire_format());
  }

  std::any Visit(const flat::Union& object) override {
    if (object.strictness == types::Strictness::kFlexible) {
      return true;
    }

    for (const auto& member : object.members) {
      if (HasFlexibleEnvelope(member, wire_format())) {
        return true;
      }
    }

    return false;
  }

  std::any Visit(const flat::Union::Member& object) override {
    return object.maybe_used ? HasFlexibleEnvelope(*object.maybe_used, wire_format()) : false;
  }

  std::any Visit(const flat::Union::Member::Used& object) override {
    return HasFlexibleEnvelope(object.type_ctor->type, wire_format());
  }

  std::any Visit(const flat::Protocol& object) override { return false; }
};

DataSize UnalignedSize(const flat::Object& object, WireFormat wire_format) {
  UnalignedSizeVisitor v(wire_format);
  return object.Accept(&v);
}

[[maybe_unused]] DataSize UnalignedSize(const flat::Object* object, WireFormat wire_format) {
  return UnalignedSize(*object, wire_format);
}

DataSize Alignment(const flat::Object& object, WireFormat wire_format) {
  AlignmentVisitor v(wire_format);
  return object.Accept(&v);
}

[[maybe_unused]] DataSize Alignment(const flat::Object* object, WireFormat wire_format) {
  return Alignment(*object, wire_format);
}

DataSize Depth(const flat::Object& object, WireFormat wire_format) {
  DepthVisitor v(wire_format);
  return object.Accept(&v);
}

[[maybe_unused]] DataSize Depth(const flat::Object* object, WireFormat wire_format) {
  return Depth(*object, wire_format);
}

DataSize MaxHandles(const flat::Object& object) {
  MaxHandlesVisitor v;
  return object.Accept(&v);
}

[[maybe_unused]] DataSize MaxHandles(const flat::Object* object) { return MaxHandles(*object); }

DataSize MaxOutOfLine(const flat::Object& object, WireFormat wire_format) {
  MaxOutOfLineVisitor v(wire_format);
  return object.Accept(&v);
}

[[maybe_unused]] DataSize MaxOutOfLine(const flat::Object* object, WireFormat wire_format) {
  return MaxOutOfLine(*object, wire_format);
}

bool HasPadding(const flat::Object& object, WireFormat wire_format) {
  HasPaddingVisitor v(wire_format);
  return object.Accept(&v);
}

[[maybe_unused]] bool HasPadding(const flat::Object* object, WireFormat wire_format) {
  return HasPadding(*object, wire_format);
}

bool HasEnvelope(const flat::Object& object, WireFormat wire_format) {
  HasEnvelopeVisitor v(wire_format);
  return object.Accept(&v);
}

[[maybe_unused]] bool HasEnvelope(const flat::Object* object, WireFormat wire_format) {
  return HasEnvelope(*object, wire_format);
}

bool HasFlexibleEnvelope(const flat::Object& object, WireFormat wire_format) {
  HasFlexibleEnvelopeVisitor v(wire_format);
  return object.Accept(&v);
}

[[maybe_unused]] bool HasFlexibleEnvelope(const flat::Object* object, WireFormat wire_format) {
  return HasFlexibleEnvelope(*object, wire_format);
}

}  // namespace

namespace fidl {

uint32_t OldWireFormatDepth(const flat::Object& object) {
  OldWireFormatDepthVisitor v(WireFormat::kV1NoEe);
  return object.Accept(&v);
}

uint32_t OldWireFormatDepth(const flat::Object* object) { return OldWireFormatDepth(*object); }

TypeShape::TypeShape(const flat::Object& object, WireFormat wire_format)
    : inline_size(::AlignedSize(object, wire_format)),
      alignment(::Alignment(object, wire_format)),
      depth(::Depth(object, wire_format)),
      max_handles(::MaxHandles(object)),
      max_out_of_line(::MaxOutOfLine(object, wire_format)),
      has_padding(::HasPadding(object, wire_format)),
      has_envelope(::HasEnvelope(object, wire_format)),
      has_flexible_envelope(::HasFlexibleEnvelope(object, wire_format)) {}

TypeShape::TypeShape(const flat::Object* object, WireFormat wire_format)
    : TypeShape(*object, wire_format) {}

TypeShape TypeShape::ForEmptyPayload() { return TypeShape(0, 0); }

FieldShape::FieldShape(const flat::StructMember& member, WireFormat wire_format) {
  ZX_ASSERT(member.parent);
  const flat::Struct& parent = *member.parent;

  // Our parent struct must have at least one member if fieldshape() on a member is being
  // called.
  ZX_ASSERT(!parent.members.empty());
  const std::vector<flat::StructMember>& members = parent.members;

  for (size_t i = 0; i < members.size(); i++) {
    const flat::StructMember* it = &members.at(i);

    DataSize alignment;
    if (i + 1 < members.size()) {
      const auto& next = members.at(i + 1);
      alignment = Alignment(next, wire_format);
    } else {
      alignment = Alignment(parent, wire_format);
    }

    uint32_t size = UnalignedSize(*it, wire_format);

    padding = ::Padding(offset + size, alignment);

    if (it == &member)
      break;

    offset += size + padding;
  }
}

FieldShape::FieldShape(const flat::TableMemberUsed& member, WireFormat wire_format)
    : padding(::Padding(UnalignedSize(member, wire_format), 8)) {}

FieldShape::FieldShape(const flat::UnionMemberUsed& member, WireFormat wire_format)
    : padding(
          ::Padding(UnalignedSize(member, wire_format), Alignment(member.parent, wire_format))) {}

}  // namespace fidl
