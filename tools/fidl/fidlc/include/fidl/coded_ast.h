// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_CODED_AST_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_CODED_AST_H_

#include <stdint.h>
#include <zircon/assert.h>

#include <limits>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "tools/fidl/fidlc/include/fidl/check.h"
#include "tools/fidl/fidlc/include/fidl/types.h"

// The types in this file define structures that much more closely map
// the coding tables (i.e., fidl_type_t) for (de)serialization,
// defined at ulib/fidl/include/coding.h and so on.

// In particular, compared to the flat_ast version:
// - All files in the library are resolved together
// - Names have been unnested and fully qualified
// - All data structure sizes and layouts have been computed

// See
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/compiler#c_family_runtime
// for additional context

namespace fidl::coded {

enum struct CodingContext {
  // The coding table of this type will be used to represent data within
  // an envelope. This will affect the 'coding needed'.
  kInsideEnvelope,

  // The coding table of this type will be used to represent data outside
  // of an envelope, and default 'coding needed' is appropriate here.
  kOutsideEnvelope,
};

struct Type;
struct StructType;

struct StructField {
  StructField(types::Resourceness resourceness, uint32_t offset_v2, const Type* type)
      : resourceness(resourceness), offset_v2(offset_v2), type(type) {}

  types::Resourceness resourceness;
  const uint32_t offset_v2;
  const Type* type;
};

struct StructPadding {
  StructPadding(uint32_t offset_v2, std::variant<uint16_t, uint32_t, uint64_t> mask)
      : offset_v2(offset_v2), mask(mask) {}

  // TODO(bprosnitz) This computes a mask for a single padding segment.
  // It is inefficient if multiple padding segments can be covered by a single mask.
  // (e.g. struct{uint8, uint16, uint8, uint16} has two padding segments but can
  // be covered by a single uint64 mask)
  static StructPadding FromLength(uint32_t offset_v2, uint32_t length) {
    ZX_ASSERT_MSG(length != 0, "padding shouldn't be created for zero-length offsets");
    if (length <= 2) {
      return StructPadding(offset_v2 & ~1, BuildMask<uint16_t>(offset_v2 & 1, length));
    }
    if (length <= 4) {
      return StructPadding(offset_v2 & ~3, BuildMask<uint32_t>(offset_v2 & 3, length));
    }
    if (length < 8) {
      return StructPadding(offset_v2 & ~7, BuildMask<uint64_t>(offset_v2 & 7, length));
    }
    ZX_PANIC("length should be < 8, got %u", length);
  }

  const uint32_t offset_v2;
  const std::variant<uint16_t, uint32_t, uint64_t> mask;

 private:
  template <typename MaskType>
  static MaskType BuildMask(uint32_t offset, uint32_t length) {
    MaskType mask = 0;
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&mask);
    for (uint32_t i = offset; i < offset + length; i++) {
      bytes[i] = 0xff;
    }
    return mask;
  }
};

using StructElement = std::variant<const StructField, const StructPadding>;

struct TableField {
  TableField(const Type* type, uint32_t ordinal) : type(type), ordinal(ordinal) {}

  const Type* type;
  const uint32_t ordinal;
};

struct XUnionField {
  explicit XUnionField(const Type* type) : type(type) {}

  const Type* type;
};

struct Type {
  virtual ~Type() = default;

  enum struct Kind : uint8_t {
    kPrimitive,
    kInternal,
    kEnum,
    kBits,
    kHandle,
    kProtocolHandle,
    kRequestHandle,
    kStruct,
    kTable,
    kXUnion,
    kStructPointer,
    kProtocol,
    kArray,
    kString,
    kVector,
    kZxExperimentalPointer,
  };

  Type(Kind kind, std::string coded_name, uint32_t size_v2, bool is_coding_needed, bool is_noop)
      : is_coding_needed(is_coding_needed),
        is_noop(is_noop),
        kind(kind),
        size_v2(size_v2),
        coded_name(std::move(coded_name)) {}

  const bool is_coding_needed;
  // is_noop indicates that the walker doesn't need to do any action on a coding table entry of
  // this type.
  // For instance, the walker can skip uint8 fields in a struct, so uint8 primitive types have
  // is_noop = true. However, bools need to be validated so bool primitive types have
  // is_noop = false.
  bool is_noop;
  const Kind kind;
  uint32_t size_v2;
  const std::string coded_name;
};

struct PrimitiveType : public Type {
  PrimitiveType(std::string name, types::PrimitiveSubtype subtype, uint32_t size,
                CodingContext context)
      : Type(Kind::kPrimitive, std::move(name), size, true,
             subtype != types::PrimitiveSubtype::kBool),
        subtype(subtype) {}

  const types::PrimitiveSubtype subtype;
};

// Internal types are types which are used internally by the bindings but not
// exposed for FIDL libraries to use.
struct InternalType : public Type {
  InternalType(std::string name, types::InternalSubtype subtype, uint32_t size,
               CodingContext context)
      : Type(Kind::kInternal, std::move(name), size, true, true), subtype(subtype) {}

  const types::InternalSubtype subtype;
};

struct EnumType : public Type {
  EnumType(std::string name, types::PrimitiveSubtype subtype, uint32_t size,
           std::vector<uint64_t> members, std::string qname, types::Strictness strictness)
      : Type(Kind::kEnum, std::move(name), size, true, false),
        subtype(subtype),
        members(std::move(members)),
        qname(std::move(qname)),
        strictness(strictness) {}

  const types::PrimitiveSubtype subtype;
  const std::vector<uint64_t> members;
  const std::string qname;
  types::Strictness strictness;
};

struct BitsType : public Type {
  BitsType(std::string name, types::PrimitiveSubtype subtype, uint32_t size, uint64_t mask,
           std::string qname, types::Strictness strictness)
      : Type(Kind::kBits, std::move(name), size, true, false),
        subtype(subtype),
        mask(mask),
        qname(std::move(qname)),
        strictness(strictness) {}

  const types::PrimitiveSubtype subtype;
  const uint64_t mask;
  const std::string qname;
  types::Strictness strictness;
};

struct HandleType : public Type {
  HandleType(std::string name, types::HandleSubtype subtype, types::RightsWrappedType rights,
             types::Nullability nullability)
      : Type(Kind::kHandle, std::move(name), 4u, true, false),
        subtype(subtype),
        rights(rights),
        nullability(nullability) {}

  const types::HandleSubtype subtype;
  const types::RightsWrappedType rights;
  const types::Nullability nullability;
};

struct ProtocolHandleType : public Type {
  ProtocolHandleType(std::string name, types::Nullability nullability)
      : Type(Kind::kProtocolHandle, std::move(name), 4u, true, false), nullability(nullability) {}

  const types::Nullability nullability;
};

struct RequestHandleType : public Type {
  RequestHandleType(std::string name, types::Nullability nullability)
      : Type(Kind::kRequestHandle, std::move(name), 4u, true, false), nullability(nullability) {}

  const types::Nullability nullability;
};

struct StructPointerType;

struct StructType : public Type {
  StructType(std::string name, std::vector<StructElement> elements, uint32_t size_v2,
             bool contains_envelope, std::string qname)
      : Type(Kind::kStruct, std::move(name), size_v2, true, false),
        elements(std::move(elements)),
        qname(std::move(qname)),
        contains_envelope(contains_envelope) {
    FIDL_CHECK(elements.size() <= std::numeric_limits<uint16_t>::max(),
               "coding table stores element_count in uint16_t");
  }

  std::vector<StructElement> elements;
  std::string qname;
  bool contains_envelope;
  bool is_empty = false;
  StructPointerType* maybe_reference_type = nullptr;
};

struct StructPointerType : public Type {
  StructPointerType(std::string name, const Type* type)
      : Type(Kind::kStructPointer, std::move(name), 8u, true, false),
        element_type(static_cast<const StructType*>(type)) {
    ZX_ASSERT(type->kind == Type::Kind::kStruct);
  }

  const StructType* element_type;
};

struct TableType : public Type {
  TableType(std::string name, std::vector<TableField> fields, std::string qname,
            types::Resourceness resourceness)
      : Type(Kind::kTable, std::move(name), 16u, true, false),
        fields(std::move(fields)),
        qname(std::move(qname)),
        resourceness(resourceness) {}

  std::vector<TableField> fields;
  std::string qname;
  types::Resourceness resourceness;
};

struct XUnionType : public Type {
  XUnionType(std::string name, std::vector<XUnionField> fields, std::string qname,
             types::Nullability nullability, types::Strictness strictness,
             types::Resourceness resourceness)
      : Type(Kind::kXUnion, std::move(name), 16u, true, false),
        fields(std::move(fields)),
        qname(std::move(qname)),
        nullability(nullability),
        strictness(strictness),
        resourceness(resourceness) {}

  std::vector<XUnionField> fields;
  const std::string qname;
  types::Nullability nullability;
  types::Strictness strictness;
  XUnionType* maybe_reference_type = nullptr;
  types::Resourceness resourceness;
};

struct ProtocolType : public Type {
  explicit ProtocolType(std::vector<std::unique_ptr<Type>> messages_during_compile)
      // N.B. ProtocolTypes are never used in the eventual coding table generation.
      : Type(Kind::kProtocol, "", 0, false, false),
        messages_during_compile(std::move(messages_during_compile)) {}

  // Note: the messages are moved from the protocol type into the
  // CodedTypesGenerator coded_types_ vector during assembly.
  std::vector<std::unique_ptr<Type>> messages_during_compile;

  // Back pointers to fully compiled message types, owned by the
  // CodedTypesGenerator coded_types_ vector.
  std::vector<const Type*> messages_after_compile;
};

struct ArrayType : public Type {
  ArrayType(std::string name, const Type* element_type, uint32_t array_size_v2,
            uint32_t element_size_v2, CodingContext context)
      : Type(Kind::kArray, std::move(name), array_size_v2, true, element_type->is_noop),
        element_type(element_type),
        element_size_v2(element_size_v2) {
    FIDL_CHECK(element_size_v2 <= std::numeric_limits<uint16_t>::max(),
               "coding table stores element_size_v2 in uint16_t");
  }

  const Type* const element_type;
  const uint32_t element_size_v2;
};

struct StringType : public Type {
  StringType(std::string name, uint32_t max_size, types::Nullability nullability)
      : Type(Kind::kString, std::move(name), 16u, true, false),
        max_size(max_size),
        nullability(nullability) {}

  const uint32_t max_size;
  const types::Nullability nullability;
};

enum struct MemcpyCompatibility {
  kCannotMemcpy,
  kCanMemcpy,
};

struct VectorType : public Type {
  VectorType(std::string name, const Type* element_type, uint32_t max_count,
             uint32_t element_size_v2, types::Nullability nullability,
             MemcpyCompatibility element_memcpy_compatibility)
      // Note: vectors have is_noop = false, but there is the potential to optimize this in the
      // future.
      : Type(Kind::kVector, std::move(name), 16u, true, false),
        element_type(element_type),
        max_count(max_count),
        element_size_v2(element_size_v2),
        nullability(nullability),
        element_memcpy_compatibility(element_memcpy_compatibility) {}

  const Type* const element_type;
  const uint32_t max_count;
  const uint32_t element_size_v2;
  const types::Nullability nullability;
  const MemcpyCompatibility element_memcpy_compatibility;
};

struct ZxExperimentalPointerType : public Type {
  ZxExperimentalPointerType(std::string name, const Type* pointee_type)
      : Type(Kind::kZxExperimentalPointer, std::move(name), 8u, true, false),
        pointee_type(pointee_type) {}

  const Type* pointee_type;
};

}  // namespace fidl::coded

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_CODED_AST_H_
