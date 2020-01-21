// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/type_shape.h"

#include <algorithm>

#include <safemath/clamped_math.h>

#include "fidl/flat_ast.h"
#include "fidl/recursion_detector.h"

namespace {

// TODO(fxb/7680): We may want to fail instead of saturating.
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

namespace flat = fidl::flat;
namespace types = fidl::types;
using WireFormat = fidl::WireFormat;

constexpr uint32_t kSizeOfTransactionHeader = 16;
constexpr uint32_t kAlignmentOfTransactionHeader = 8;
constexpr uint32_t kHandleSize = 4;

DataSize UnalignedSize(const flat::Object& object, const WireFormat wire_format);
DataSize UnalignedSize(const flat::Object* object, const WireFormat wire_format);
DataSize Alignment(const flat::Object& object, const WireFormat wire_format);
DataSize Alignment(const flat::Object* object, const WireFormat wire_format);
DataSize Depth(const flat::Object& object, const WireFormat wire_format);
DataSize Depth(const flat::Object* object, const WireFormat wire_format);
DataSize MaxHandles(const flat::Object& object);
DataSize MaxHandles(const flat::Object* object);
DataSize MaxOutOfLine(const flat::Object& object, const WireFormat wire_format);
DataSize MaxOutOfLine(const flat::Object* object, const WireFormat wire_format);
bool HasPadding(const flat::Object& object, const WireFormat wire_format);
bool HasPadding(const flat::Object* object, const WireFormat wire_format);
bool HasFlexibleEnvelope(const flat::Object& object);
bool HasFlexibleEnvelope(const flat::Object* object);
bool ContainsUnion(const flat::Object& object);
bool ContainsUnion(const flat::Object* object);

DataSize AlignedSize(const flat::Object& object, const WireFormat wire_format) {
  return AlignTo(UnalignedSize(object, wire_format), Alignment(object, wire_format));
}

DataSize AlignedSize(const flat::Object* object, const WireFormat wire_format) {
  return AlignedSize(*object, wire_format);
}

template <typename T>
class TypeShapeVisitor : public flat::Object::Visitor<T> {
 public:
  TypeShapeVisitor() = delete;
  explicit TypeShapeVisitor(const WireFormat wire_format) : wire_format_(wire_format) {}

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
      case types::PrimitiveSubtype::kFloat64:
        return DataSize(8);
    }
  }

  std::any Visit(const flat::IdentifierType& object) override {
    switch (object.nullability) {
      case types::Nullability::kNullable:
        switch (object.type_decl->kind) {
          case flat::Decl::Kind::kProtocol:
          case flat::Decl::Kind::kService:
            return DataSize(kHandleSize);
          case flat::Decl::Kind::kStruct:
            return DataSize(8);
          case flat::Decl::Kind::kUnion:
            switch (wire_format()) {
              case WireFormat::kOld:
                return DataSize(8);
              case WireFormat::kV1NoEe:
              case WireFormat::kV1Header:
                return DataSize(24);
            }
          case flat::Decl::Kind::kXUnion:
            return DataSize(24);
          case flat::Decl::Kind::kBits:
          case flat::Decl::Kind::kConst:
          case flat::Decl::Kind::kEnum:
          case flat::Decl::Kind::kTable:
          case flat::Decl::Kind::kTypeAlias:
            assert(false && "UnalignedSize(flat::IdentifierType&) called on invalid nullable kind");
            return DataSize(0);
        }
      case types::Nullability::kNonnullable: {
        return UnalignedSize(object.type_decl);
      }
    }
  }

  std::any Visit(const flat::RequestHandleType& object) override { return DataSize(kHandleSize); }

  std::any Visit(const flat::Enum& object) override {
    return UnalignedSize(object.subtype_ctor->type);
  }

  std::any Visit(const flat::Bits& object) override {
    return UnalignedSize(object.subtype_ctor->type);
  }

  std::any Visit(const flat::Service& object) override { return DataSize(kHandleSize); }

  std::any Visit(const flat::Struct& object) override {
    if (object.members.empty()) {
      // Object is an empty struct
      if (object.is_request_or_response && wire_format() != WireFormat::kV1Header) {
        return DataSize(kSizeOfTransactionHeader);
      }
      return DataSize(1);
    }

    DataSize size = 0;

    if (object.is_request_or_response && wire_format() != WireFormat::kV1Header) {
      size += kSizeOfTransactionHeader;
    }

    for (const auto& member : object.members) {
      const DataSize member_size =
          UnalignedSize(member) + member.fieldshape(wire_format()).Padding();
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
      case WireFormat::kOld: {
        DataSize max_member_size = 0;

        for (const auto& member : object.members) {
          max_member_size = std::max(max_member_size, UnalignedSize(member));
        }

        // * The size of the union's tag is always 4 bytes.
        // * However, the _aligned_ size of the union's tag (which is the unaligned size + padding)
        // will
        //   be:
        //   * 4 iff all union members have alignment <= 4, or
        //   * 8 iff any of the union members have alignment 8.
        // * In other words, the size of the union's tag is the same as the alignment for the union.
        // * It would be more readable to call object.DataOffset() here, but we cannot do that,
        // since
        //   object.Offset() calls this code--Alignment()--internally.
        const DataSize tag_size = Alignment(object, wire_format());

        // TODO(fxb/38129): The code in the line below returns the _aligned_ size of the union, not
        // the _unaligned_ size. To fix this, we need to remove the call to AlignTo(), so this
        // should just be "return tag_size + max_member_size". The current code is like this to
        // preserve backward-compatibility with the previous typeshape implementation.
        return tag_size + AlignTo(max_member_size, tag_size);
      }
      case WireFormat::kV1NoEe:
      case WireFormat::kV1Header:
        return DataSize(24);
    }
  }

  std::any Visit(const flat::Union::Member& object) override {
    return object.maybe_used ? UnalignedSize(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::Union::Member::Used& object) override {
    return UnalignedSize(object.type_ctor->type);
  }

  std::any Visit(const flat::XUnion& object) override { return DataSize(24); }

  std::any Visit(const flat::XUnion::Member& object) override {
    return object.maybe_used ? UnalignedSize(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::XUnion::Member::Used& object) override {
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

  std::any Visit(const flat::IdentifierType& object) override {
    switch (object.nullability) {
      case types::Nullability::kNullable:
        switch (object.type_decl->kind) {
          case flat::Decl::Kind::kProtocol:
          case flat::Decl::Kind::kService:
            return DataSize(kHandleSize);
          case flat::Decl::Kind::kStruct:
          case flat::Decl::Kind::kUnion:
          case flat::Decl::Kind::kXUnion:
            return DataSize(8);
          case flat::Decl::Kind::kBits:
          case flat::Decl::Kind::kConst:
          case flat::Decl::Kind::kEnum:
          case flat::Decl::Kind::kTable:
          case flat::Decl::Kind::kTypeAlias:
            assert(false && "Alignment(flat::IdentifierType&) called on invalid nullable kind");
            return DataSize(0);
        }
      case types::Nullability::kNonnullable:
        return Alignment(object.type_decl);
    }
  }

  std::any Visit(const flat::RequestHandleType& object) override { return DataSize(kHandleSize); }

  std::any Visit(const flat::Enum& object) override { return Alignment(object.subtype_ctor->type); }

  std::any Visit(const flat::Bits& object) override { return Alignment(object.subtype_ctor->type); }

  std::any Visit(const flat::Service& object) override { return DataSize(kHandleSize); }

  std::any Visit(const flat::Struct& object) override {
    if (object.recursive) {
      // |object| is recursive, therefore there must be a pointer to this struct in the recursion
      // chain, with pointer-sized alignment.
      return DataSize(8);
    }

    if (object.is_request_or_response) {
      // Request/response structs have an alignment of 8. (Note that this was a bug before FTP-029,
      // which changed method ordinals from 32 to 64 bits. Before FTP-029, the assumed alignment was
      // 4, but in practice, all FIDL bindings and typeshape calculation code were assuming a
      // minimum alignment of 8.)
      return DataSize(kAlignmentOfTransactionHeader);
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

  std::any Visit(const flat::Union& object) override {
    switch (wire_format()) {
      case WireFormat::kOld: {
        // Union member alignment is either 4 or 8, depending on whether any union members have
        // alignment 8.

        for (const auto& member : object.members) {
          if (Alignment(member) == 8) {
            return DataSize(8);
          }
        }

        return DataSize(4);
      }
      case WireFormat::kV1NoEe:
      case WireFormat::kV1Header:
        return DataSize(8);
    }
  }

  std::any Visit(const flat::Union::Member& object) override {
    return object.maybe_used ? Alignment(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::UnionMember::Used& object) override {
    return Alignment(object.type_ctor->type);
  }

  std::any Visit(const flat::XUnion& object) override { return DataSize(8); }

  std::any Visit(const flat::XUnion::Member& object) override {
    return object.maybe_used ? Alignment(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::XUnion::Member::Used& object) override {
    return Alignment(object.type_ctor->type);
  }

  std::any Visit(const flat::Protocol& object) override { return DataSize(kHandleSize); }

 private:
  DataSize Alignment(const flat::Object& object) { return object.Accept(this); }

  DataSize Alignment(const flat::Object* object) { return Alignment(*object); }
};

class DepthVisitor final : public TypeShapeVisitor<DataSize> {
 public:
  using TypeShapeVisitor<DataSize>::TypeShapeVisitor;

  std::any Visit(const flat::ArrayType& object) override { return Depth(object.element_type); }

  std::any Visit(const flat::VectorType& object) override {
    return DataSize(1) + Depth(object.element_type);
  }

  std::any Visit(const flat::StringType& object) override { return DataSize(1); }

  std::any Visit(const flat::HandleType& object) override { return DataSize(0); }

  std::any Visit(const flat::PrimitiveType& object) override { return DataSize(0); }

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
            switch (wire_format()) {
              case WireFormat::kOld:
                return DataSize(1) + Depth(object.type_decl);
              case WireFormat::kV1NoEe:
              case WireFormat::kV1Header:
                return Depth(object.type_decl);
            }
          case flat::Decl::Kind::kXUnion:
            return Depth(object.type_decl);
          case flat::Decl::Kind::kBits:
          case flat::Decl::Kind::kConst:
          case flat::Decl::Kind::kEnum:
          case flat::Decl::Kind::kTable:
          case flat::Decl::Kind::kTypeAlias:
            assert(false && "Depth(flat::IdentifierType&) called on invalid nullable kind");
            return DataSize(0);
        }
      case types::Nullability::kNonnullable:
        switch (object.type_decl->kind) {
          case flat::Decl::Kind::kProtocol:
          case flat::Decl::Kind::kService:
          case flat::Decl::Kind::kBits:
          case flat::Decl::Kind::kConst:
          case flat::Decl::Kind::kEnum:
            return DataSize(0);
          case flat::Decl::Kind::kUnion:
          case flat::Decl::Kind::kXUnion:
          case flat::Decl::Kind::kTable:
          case flat::Decl::Kind::kTypeAlias:
          case flat::Decl::Kind::kStruct:
            return Depth(object.type_decl);
        }
    }
  }

  std::any Visit(const flat::RequestHandleType& object) override { return DataSize(0); }

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

    switch (wire_format()) {
      case WireFormat::kOld:
        return max_depth;
      case WireFormat::kV1NoEe:
      case WireFormat::kV1Header:
        return DataSize(1) + max_depth;
    }
  }

  std::any Visit(const flat::Union::Member& object) override {
    return object.maybe_used ? Depth(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::Union::Member::Used& object) override {
    return Depth(object.type_ctor->type);
  }

  std::any Visit(const flat::XUnion& object) override {
    DataSize max_depth = 0;

    for (const auto& member : object.members) {
      max_depth = std::max(max_depth, Depth(member));
    }

    return DataSize(1) + max_depth;
  }

  std::any Visit(const flat::XUnion::Member& object) override {
    return object.maybe_used ? Depth(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::XUnion::Member::Used& object) override {
    return Depth(object.type_ctor->type);
  }

  std::any Visit(const flat::Protocol& object) override { return DataSize(0); }

 private:
  DataSize Depth(const flat::Object& object) { return object.Accept(this); }

  DataSize Depth(const flat::Object* object) { return Depth(*object); }
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

  std::any Visit(const flat::IdentifierType& object) override {
    thread_local RecursionDetector recursion_detector;

    // TODO(fxb/36327): This code is technically incorrect; see the visit(Struct&) overload for more
    // details.
    auto guard = recursion_detector.Enter(&object);
    if (!guard) {
      return DataSize(0);
    }

    return MaxHandles(object.type_decl);
  }

  std::any Visit(const flat::RequestHandleType& object) override { return DataSize(1); }

  std::any Visit(const flat::Enum& object) override {
    return MaxHandles(object.subtype_ctor->type);
  }

  std::any Visit(const flat::Bits& object) override {
    return MaxHandles(object.subtype_ctor->type);
  }

  std::any Visit(const flat::Service& object) override { return DataSize(1); }

  std::any Visit(const flat::Struct& object) override {
    // TODO(fxb/36327): This is technically incorrect: if a struct is recursive, it may not directly
    // contain a handle, but could contain e.g. a struct that contains a handle. In that case, this
    // code will return 0 instead of std::numeric_limits<DataSize>::max(). This does pass all
    // current tests and Fuchsia compilation, so fixing it isn't super-urgent.
    if (object.recursive) {
      for (const auto& member : object.members) {
        switch (member.type_ctor->type->kind) {
          case flat::Type::Kind::kHandle:
          case flat::Type::Kind::kRequestHandle:
            return std::numeric_limits<DataSize>::max();
          case flat::Type::Kind::kArray:
          case flat::Type::Kind::kVector:
          case flat::Type::Kind::kString:
          case flat::Type::Kind::kPrimitive:
          case flat::Type::Kind::kIdentifier:
            continue;
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

  std::any Visit(const flat::XUnion& object) override {
    DataSize max_handles;

    for (const auto& member : object.members) {
      max_handles = std::max(max_handles, MaxHandles(member));
    }

    return max_handles;
  }

  std::any Visit(const flat::XUnion::Member& object) override {
    return object.maybe_used ? MaxHandles(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::XUnion::Member::Used& object) override {
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
            switch (wire_format()) {
              case WireFormat::kOld:
                return ObjectAlign(UnalignedSize(object.type_decl, wire_format())) +
                       MaxOutOfLine(object.type_decl);
              case WireFormat::kV1NoEe:
              case WireFormat::kV1Header:
                return MaxOutOfLine(object.type_decl);
            }
          case flat::Decl::Kind::kXUnion:
            return MaxOutOfLine(object.type_decl);
          case flat::Decl::Kind::kBits:
          case flat::Decl::Kind::kConst:
          case flat::Decl::Kind::kEnum:
          case flat::Decl::Kind::kTable:
          case flat::Decl::Kind::kTypeAlias:
            assert(false && "MaxOutOfLine(flat::IdentifierType&) called on invalid nullable kind");
            return 0;
        }
      }
      case types::Nullability::kNonnullable:
        return MaxOutOfLine(object.type_decl);
    }
  }

  std::any Visit(const flat::RequestHandleType& object) override { return DataSize(0); }

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
    size_t envelope_array_size = 0;

    for (const auto& member : object.members) {
      max_out_of_line += ObjectAlign(UnalignedSize(member, wire_format())) + MaxOutOfLine(member);

      // TODO(fxb/35773): The following if() check excludes reserved table members from the
      // out-of-line calculation, which is incorrect. The correct thing to do is to _include_
      // reserved table fields in the out-of-line-calculation, and only exclude reserved fields if
      // they're at the end of the table definition. The code is like this to preserve bug-for-bug
      // compatibility with the previous typeshape implementation. See the bug for more details.
      if (member.maybe_used)
        envelope_array_size++;
    }

    constexpr DataSize kEnvelopeSize = 16;
    return DataSize(envelope_array_size) * kEnvelopeSize + max_out_of_line;
  }

  std::any Visit(const flat::Table::Member& object) override {
    return object.maybe_used ? MaxOutOfLine(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::Table::Member::Used& object) override {
    return ObjectAlign(MaxOutOfLine(object.type_ctor->type));
  }

  std::any Visit(const flat::Union& object) override {
    DataSize max_out_of_line;

    for (const auto& member : object.members) {
      max_out_of_line = std::max(max_out_of_line, [&] {
        switch (wire_format()) {
          case WireFormat::kOld:
            return MaxOutOfLine(member);
          case WireFormat::kV1NoEe:
          case WireFormat::kV1Header:
            // Same as XUnions.
            return ObjectAlign(UnalignedSize(member, wire_format())) + MaxOutOfLine(member);
        }
      }());
    }

    return max_out_of_line;
  }

  std::any Visit(const flat::Union::Member& object) override {
    return object.maybe_used ? MaxOutOfLine(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::Union::Member::Used& object) override {
    return MaxOutOfLine(object.type_ctor->type);
  }

  std::any Visit(const flat::XUnion& object) override {
    DataSize max_out_of_line = 0;

    for (const auto& member : object.members) {
      max_out_of_line =
          std::max(max_out_of_line,
                   ObjectAlign(UnalignedSize(member, wire_format())) + MaxOutOfLine(member));
    }

    return max_out_of_line;
  }

  std::any Visit(const flat::XUnion::Member& object) override {
    return object.maybe_used ? MaxOutOfLine(*object.maybe_used) : DataSize(0);
  }

  std::any Visit(const flat::XUnion::Member::Used& object) override {
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
      if (Padding(UnalignedSize(object.element_type, wire_format()), 8) == 0) {
        return false;
      }

      return true;
    };

    return element_has_trailing_padding() || element_has_innate_padding();
  }

  std::any Visit(const flat::StringType& object) override { return true; }

  std::any Visit(const flat::HandleType& object) override { return false; }

  std::any Visit(const flat::PrimitiveType& object) override { return false; }

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
          case flat::Decl::Kind::kStruct:
          case flat::Decl::Kind::kUnion:
            return Padding(UnalignedSize(object.type_decl, wire_format()), 8) > 0 ||
                   HasPadding(object.type_decl);
          case flat::Decl::Kind::kXUnion:
            return HasPadding(object.type_decl);
          case flat::Decl::Kind::kBits:
          case flat::Decl::Kind::kConst:
          case flat::Decl::Kind::kEnum:
          case flat::Decl::Kind::kTable:
          case flat::Decl::Kind::kTypeAlias:
            assert(false && "HasPadding(flat::IdentifierType&) called on invalid nullable kind");
            return false;
        }
      case types::Nullability::kNonnullable:
        return HasPadding(object.type_decl);
    }
  }

  std::any Visit(const flat::RequestHandleType& object) override { return false; }

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
    return object.fieldshape(wire_format()).Padding() > 0 || HasPadding(object.type_ctor->type);
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
           HasPadding(object.type_ctor->type) || object.fieldshape(wire_format()).Padding() > 0;
  }

  std::any Visit(const flat::Union& object) override {
    switch (wire_format()) {
      case WireFormat::kOld: {
        if (Alignment(object, wire_format()) == 8) {
          // Unions have a 32-bit tag, so if a union's alignment is 8, it is necessarily padded.
          return true;
        }

        for (const auto& member : object.members) {
          if (HasPadding(member)) {
            return true;
          }
        }

        return false;
      }
      case WireFormat::kV1Header:
      case WireFormat::kV1NoEe: {
        // TODO(fxb/36332): XUnions currently return true for has_padding in all cases, which should
        // be fixed.
        return true;
      }
    }
  }

  std::any Visit(const flat::Union::Member& object) override {
    return object.maybe_used ? HasPadding(*object.maybe_used) : false;
  }

  std::any Visit(const flat::Union::Member::Used& object) override {
    // TODO(fxb/36331): This code only accounts for inline padding for the union member. We also
    // need to account for out-of-line padding.
    return object.fieldshape(wire_format()).Padding() > 0;
  }

  std::any Visit(const flat::XUnion& object) override {
    // TODO(fxb/36332): XUnions currently return true for has_padding in all cases, which should be
    // fixed.
    return true;
  }

  std::any Visit(const flat::XUnion::Member& object) override {
    return object.maybe_used ? HasPadding(*object.maybe_used) : false;
  }

  std::any Visit(const flat::XUnion::Member::Used& object) override {
    // TODO(fxb/36332): This code only accounts for inline padding for the xunion member. We should
    // also account for out-of-line padding.
    return object.fieldshape(wire_format()).Padding() > 0;
  }

  std::any Visit(const flat::Protocol& object) override { return false; }

 private:
  bool HasPadding(const flat::Object& object) { return object.Accept(this); }

  bool HasPadding(const flat::Object* object) { return HasPadding(*object); }
};

class HasFlexibleEnvelopeVisitor final : public flat::Object::Visitor<bool> {
 public:
  std::any Visit(const flat::ArrayType& object) override {
    return HasFlexibleEnvelope(object.element_type);
  }

  std::any Visit(const flat::VectorType& object) override {
    return HasFlexibleEnvelope(object.element_type);
  }

  std::any Visit(const flat::StringType& object) override { return false; }

  std::any Visit(const flat::HandleType& object) override { return false; }

  std::any Visit(const flat::PrimitiveType& object) override { return false; }

  std::any Visit(const flat::IdentifierType& object) override {
    thread_local RecursionDetector recursion_detector;

    auto guard = recursion_detector.Enter(&object);
    if (!guard) {
      return false;
    }

    return HasFlexibleEnvelope(object.type_decl);
  }

  std::any Visit(const flat::RequestHandleType& object) override { return false; }

  std::any Visit(const flat::Enum& object) override {
    return HasFlexibleEnvelope(object.subtype_ctor->type);
  }

  std::any Visit(const flat::Bits& object) override {
    return HasFlexibleEnvelope(object.subtype_ctor->type);
  }

  std::any Visit(const flat::Service& object) override { return false; }

  std::any Visit(const flat::Struct& object) override {
    for (const auto& member : object.members) {
      if (HasFlexibleEnvelope(member)) {
        return true;
      }
    }

    return false;
  }

  std::any Visit(const flat::Struct::Member& object) override {
    return HasFlexibleEnvelope(object.type_ctor->type);
  }

  std::any Visit(const flat::Table& object) override {
    if (object.strictness == types::Strictness::kFlexible) {
      return true;
    }

    for (const auto& member : object.members) {
      if (HasFlexibleEnvelope(member)) {
        return true;
      }
    }

    return false;
  }

  std::any Visit(const flat::Table::Member& object) override {
    return object.maybe_used ? HasFlexibleEnvelope(*object.maybe_used) : false;
  }

  std::any Visit(const flat::Table::Member::Used& object) override {
    return HasFlexibleEnvelope(object.type_ctor->type);
  }

  std::any Visit(const flat::Union& object) override {
    for (const auto& member : object.members) {
      if (HasFlexibleEnvelope(member)) {
        return true;
      }
    }

    return false;
  }

  std::any Visit(const flat::Union::Member& object) override {
    return object.maybe_used ? HasFlexibleEnvelope(*object.maybe_used) : false;
  }

  std::any Visit(const flat::Union::Member::Used& object) override {
    return HasFlexibleEnvelope(object.type_ctor->type);
  }

  std::any Visit(const flat::XUnion& object) override {
    if (object.strictness == types::Strictness::kFlexible) {
      return true;
    }

    for (const auto& member : object.members) {
      if (HasFlexibleEnvelope(member)) {
        return true;
      }
    }

    return false;
  }

  std::any Visit(const flat::XUnion::Member& object) override {
    return object.maybe_used ? HasFlexibleEnvelope(*object.maybe_used) : false;
  }

  std::any Visit(const flat::XUnion::Member::Used& object) override {
    return HasFlexibleEnvelope(object.type_ctor->type);
  }

  std::any Visit(const flat::Protocol& object) override { return false; }
};

class ContainsUnionVisitor final : public flat::Object::Visitor<bool> {
 public:
  std::any Visit(const flat::ArrayType& object) override {
    return ContainsUnion(object.element_type);
  }

  std::any Visit(const flat::VectorType& object) override {
    return ContainsUnion(object.element_type);
  }

  std::any Visit(const flat::StringType& object) override { return false; }

  std::any Visit(const flat::HandleType& object) override { return false; }

  std::any Visit(const flat::PrimitiveType& object) override { return false; }

  std::any Visit(const flat::IdentifierType& object) override {
    thread_local RecursionDetector recursion_detector;

    auto guard = recursion_detector.Enter(&object);
    if (!guard) {
      return false;
    }

    return ContainsUnion(object.type_decl);
  }

  std::any Visit(const flat::RequestHandleType& object) override { return false; }

  std::any Visit(const flat::Enum& object) override {
    return ContainsUnion(object.subtype_ctor->type);
  }

  std::any Visit(const flat::Bits& object) override {
    return ContainsUnion(object.subtype_ctor->type);
  }

  std::any Visit(const flat::Service& object) override { return false; }

  std::any Visit(const flat::Struct& object) override {
    for (const auto& member : object.members) {
      if (ContainsUnion(member)) {
        return true;
      }
    }

    return false;
  }

  std::any Visit(const flat::Struct::Member& object) override {
    return ContainsUnion(object.type_ctor->type);
  }

  std::any Visit(const flat::Table& object) override {
    for (const auto& member : object.members) {
      if (ContainsUnion(member)) {
        return true;
      }
    }

    return false;
  }

  std::any Visit(const flat::Table::Member& object) override {
    return object.maybe_used ? ContainsUnion(*object.maybe_used) : false;
  }

  std::any Visit(const flat::Table::Member::Used& object) override {
    return ContainsUnion(object.type_ctor->type);
  }

  std::any Visit(const flat::Union& object) override { return true; }

  std::any Visit(const flat::Union::Member& object) override {
    return object.maybe_used ? ContainsUnion(*object.maybe_used) : false;
  }

  std::any Visit(const flat::Union::Member::Used& object) override {
    return ContainsUnion(object.type_ctor->type);
  }

  std::any Visit(const flat::XUnion& object) override {
    for (const auto& member : object.members) {
      if (ContainsUnion(member)) {
        return true;
      }
    }

    return false;
  }

  std::any Visit(const flat::XUnion::Member& object) override {
    return object.maybe_used ? ContainsUnion(*object.maybe_used) : false;
  }

  std::any Visit(const flat::XUnion::Member::Used& object) override {
    return ContainsUnion(object.type_ctor->type);
  }

  std::any Visit(const flat::Protocol& object) override { return false; }
};

DataSize UnalignedSize(const flat::Object& object, const WireFormat wire_format) {
  UnalignedSizeVisitor v(wire_format);
  return object.Accept(&v);
}

DataSize UnalignedSize(const flat::Object* object, const WireFormat wire_format) {
  return UnalignedSize(*object, wire_format);
}

DataSize Alignment(const flat::Object& object, const WireFormat wire_format) {
  AlignmentVisitor v(wire_format);
  return object.Accept(&v);
}

DataSize Alignment(const flat::Object* object, const WireFormat wire_format) {
  return Alignment(*object, wire_format);
}

DataSize Depth(const flat::Object& object, const WireFormat wire_format) {
  DepthVisitor v(wire_format);
  return object.Accept(&v);
}

DataSize Depth(const flat::Object* object, const WireFormat wire_format) {
  return Depth(*object, wire_format);
}

DataSize MaxHandles(const flat::Object& object) {
  MaxHandlesVisitor v;
  return object.Accept(&v);
}

DataSize MaxHandles(const flat::Object* object) { return MaxHandles(*object); }

DataSize MaxOutOfLine(const flat::Object& object, const WireFormat wire_format) {
  MaxOutOfLineVisitor v(wire_format);
  return object.Accept(&v);
}

DataSize MaxOutOfLine(const flat::Object* object, const WireFormat wire_format) {
  return MaxOutOfLine(*object, wire_format);
}

bool HasPadding(const flat::Object& object, const WireFormat wire_format) {
  HasPaddingVisitor v(wire_format);
  return object.Accept(&v);
}

bool HasPadding(const flat::Object* object, const WireFormat wire_format) {
  return HasPadding(*object, wire_format);
}

bool HasFlexibleEnvelope(const flat::Object& object) {
  HasFlexibleEnvelopeVisitor v;
  return object.Accept(&v);
}

bool HasFlexibleEnvelope(const flat::Object* object) { return HasFlexibleEnvelope(*object); }

bool ContainsUnion(const flat::Object& object) {
  ContainsUnionVisitor v;
  return object.Accept(&v);
}

bool ContainsUnion(const flat::Object* object) { return ContainsUnion(*object); }

}  // namespace

namespace fidl {

TypeShape::TypeShape(const flat::Object& object, WireFormat wire_format)
    : inline_size(::AlignedSize(object, wire_format)),
      alignment(::Alignment(object, wire_format)),
      depth(::Depth(object, wire_format)),
      max_handles(::MaxHandles(object)),
      max_out_of_line(::MaxOutOfLine(object, wire_format)),
      has_padding(::HasPadding(object, wire_format)),
      has_flexible_envelope(::HasFlexibleEnvelope(object)),
      contains_union(::ContainsUnion(object)) {}

TypeShape::TypeShape(const flat::Object* object, WireFormat wire_format)
    : TypeShape(*object, wire_format) {}

FieldShape::FieldShape(const flat::StructMember& member, const WireFormat wire_format) {
  assert(member.parent);
  const flat::Struct& parent = *member.parent;

  // Our parent struct must have at least one member if fieldshape() on a member is being
  // called.
  assert(parent.members.size());
  const std::vector<flat::StructMember>& members = parent.members;

  if (parent.is_request_or_response && wire_format != WireFormat::kV1Header) {
    offset += kSizeOfTransactionHeader;
  }

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

FieldShape::FieldShape(const flat::TableMemberUsed& member, const WireFormat wire_format)
    : padding(::Padding(UnalignedSize(member, wire_format), 8)) {}

FieldShape::FieldShape(const flat::UnionMemberUsed& member, const WireFormat wire_format)
    : offset([&] {
        switch (wire_format) {
          case WireFormat::kOld:
            return Alignment(member.parent, wire_format).RawValue();
          case WireFormat::kV1NoEe:
          case WireFormat::kV1Header:
            return 0u;
        }
      }()),
      padding([&] {
        switch (wire_format) {
          case WireFormat::kOld:
            return AlignedSize(member.parent, wire_format) - offset -
                   UnalignedSize(member, wire_format).RawValue();
          case WireFormat::kV1NoEe:
          case WireFormat::kV1Header:
            // Same as XUnions.
            return ::Padding(UnalignedSize(member, wire_format),
                             Alignment(member.parent, wire_format));
        }
      }()) {}

FieldShape::FieldShape(const flat::XUnionMemberUsed& member, const WireFormat wire_format)
    : padding(
          ::Padding(UnalignedSize(member, wire_format), Alignment(member.parent, wire_format))) {}

}  // namespace fidl
