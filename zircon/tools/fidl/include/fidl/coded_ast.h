// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_CODED_AST_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_CODED_AST_H_

#include <stdint.h>

#include <cassert>
#include <limits>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "check.h"
#include "types.h"

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

namespace fidl {
namespace coded {

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
  StructField(bool is_resource, uint32_t offset, const Type* type)
      : is_resource(is_resource), offset(offset), type(type) {}

  bool is_resource;
  const uint32_t offset;
  const Type* type;
};

struct StructPadding {
  StructPadding(uint32_t offset, std::variant<uint16_t, uint32_t, uint64_t> mask)
      : offset(offset), mask(mask) {}

  // TODO(bprosnitz) This computes a mask for a single padding segment.
  // It is inefficient if multiple padding segments can be covered by a single mask.
  // (e.g. struct{uint8, uint16, uint8, uint16} has two padding segments but can
  // be covered by a single uint64 mask)
  static StructPadding FromLength(uint32_t offset, uint32_t length) {
    assert(length != 0 && "padding shouldn't be created for zero-length offsets");
    if (length <= 2) {
      return StructPadding(offset & ~1, BuildMask<uint16_t>(offset & 1, length));
    } else if (length <= 4) {
      return StructPadding(offset & ~3, BuildMask<uint32_t>(offset & 3, length));
    } else if (length < 8) {
      return StructPadding(offset & ~7, BuildMask<uint64_t>(offset & 7, length));
    } else {
      assert(false && "length should be < 8");
    }
    __builtin_unreachable();
  }

  const uint32_t offset;
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
  XUnionField(const Type* type) : type(type) {}

  const Type* type;
};

struct Type {
  virtual ~Type() = default;

  enum struct Kind : uint8_t {
    kPrimitive,
    kEnum,
    kBits,
    kHandle,
    kProtocolHandle,
    kRequestHandle,
    kStruct,
    kTable,
    kXUnion,
    kStructPointer,
    kMessage,
    kProtocol,
    kArray,
    kString,
    kVector,
  };

  Type(Kind kind, std::string coded_name, uint32_t size, bool is_coding_needed, bool is_noop)
      : is_coding_needed(is_coding_needed),
        is_noop(is_noop),
        kind(kind),
        size(size),
        coded_name(std::move(coded_name)) {}

  const bool is_coding_needed;
  // is_noop indicates that the walker doesn't need to do any action on a coding table entry of
  // this type.
  // For instance, the walker can skip uint8 fields in a struct, so uint8 primitive types have
  // is_noop = true. However, bools need to be validated so bool primitive types have
  // is_noop = false.
  bool is_noop;
  const Kind kind;
  uint32_t size;
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

struct EnumType : public Type {
  EnumType(std::string name, types::PrimitiveSubtype subtype, uint32_t size,
           std::vector<uint64_t> members, std::string qname)
      : Type(Kind::kEnum, std::move(name), size, true, false),
        subtype(subtype),
        members(std::move(members)),
        qname(std::move(qname)) {}

  const types::PrimitiveSubtype subtype;
  const std::vector<uint64_t> members;
  const std::string qname;
};

struct BitsType : public Type {
  BitsType(std::string name, types::PrimitiveSubtype subtype, uint32_t size, uint64_t mask,
           std::string qname)
      : Type(Kind::kBits, std::move(name), size, true, false),
        subtype(subtype),
        mask(mask),
        qname(std::move(qname)) {}

  const types::PrimitiveSubtype subtype;
  const uint64_t mask;
  const std::string qname;
};

struct HandleType : public Type {
  HandleType(std::string name, types::HandleSubtype subtype, types::Rights rights,
             types::Nullability nullability)
      : Type(Kind::kHandle, std::move(name), 4u, true, false),
        subtype(subtype),
        rights(rights),
        nullability(nullability) {}

  const types::HandleSubtype subtype;
  const types::Rights rights;
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
  StructType(std::string name, std::vector<StructElement> elements, uint32_t size,
             std::string qname)
      : Type(Kind::kStruct, std::move(name), size, true, false),
        elements(std::move(elements)),
        qname(std::move(qname)) {
    FIDL_CHECK(elements.size() <= std::numeric_limits<uint16_t>::max(),
               "coding table stores element_count in uint16_t");
  }

  std::vector<StructElement> elements;
  std::string qname;
  StructPointerType* maybe_reference_type = nullptr;
};

struct StructPointerType : public Type {
  StructPointerType(std::string name, const Type* type, const uint32_t pointer_size)
      : Type(Kind::kStructPointer, std::move(name), pointer_size, true, false),
        element_type(static_cast<const StructType*>(type)) {
    assert(type->kind == Type::Kind::kStruct);
  }

  const StructType* element_type;
};

struct TableType : public Type {
  TableType(std::string name, std::vector<TableField> fields, uint32_t size, std::string qname)
      : Type(Kind::kTable, std::move(name), size, true, false),
        fields(std::move(fields)),
        qname(std::move(qname)) {}

  std::vector<TableField> fields;
  std::string qname;
};

struct XUnionType : public Type {
  XUnionType(std::string name, std::vector<XUnionField> fields, std::string qname,
             types::Nullability nullability, types::Strictness strictness)
      : Type(Kind::kXUnion, std::move(name), 24u, true, false),
        fields(std::move(fields)),
        qname(std::move(qname)),
        nullability(nullability),
        strictness(strictness) {}

  std::vector<XUnionField> fields;
  const std::string qname;
  types::Nullability nullability;
  types::Strictness strictness;
  XUnionType* maybe_reference_type = nullptr;
};

struct MessageType : public Type {
  MessageType(std::string name, std::vector<StructElement> elements, uint32_t size,
              std::string qname)
      : Type(Kind::kMessage, std::move(name), size, true, false),
        elements(std::move(elements)),
        qname(std::move(qname)) {}

  std::vector<StructElement> elements;
  std::string qname;
};

struct ProtocolType : public Type {
  explicit ProtocolType(std::vector<std::unique_ptr<MessageType>> messages_during_compile)
      // N.B. ProtocolTypes are never used in the eventual coding table generation.
      : Type(Kind::kProtocol, "", 0, false, false),
        messages_during_compile(std::move(messages_during_compile)) {}

  // Note: the messages are moved from the protocol type into the
  // CodedTypesGenerator coded_types_ vector during assembly.
  std::vector<std::unique_ptr<MessageType>> messages_during_compile;

  // Back pointers to fully compiled message types, owned by the
  // CodedTypesGenerator coded_types_ vector.
  std::vector<const MessageType*> messages_after_compile;
};

struct ArrayType : public Type {
  ArrayType(std::string name, const Type* element_type, uint32_t array_size, uint32_t element_size,
            CodingContext context)
      : Type(Kind::kArray, std::move(name), array_size, true, element_type->is_noop),
        element_type(element_type),
        element_size(element_size) {
    FIDL_CHECK(element_size <= std::numeric_limits<uint16_t>::max(),
               "coding table stores element_size in uint16_t");
  }

  const Type* const element_type;
  const uint32_t element_size;
};

struct StringType : public Type {
  StringType(std::string name, uint32_t max_size, types::Nullability nullability)
      : Type(Kind::kString, std::move(name), 16u, true, false),
        max_size(max_size),
        nullability(nullability) {}

  const uint32_t max_size;
  const types::Nullability nullability;
};

struct VectorType : public Type {
  VectorType(std::string name, const Type* element_type, uint32_t max_count, uint32_t element_size,
             types::Nullability nullability)
      // Note: vectors have is_noop = false, but there is the potential to optimize this in the
      // future.
      : Type(Kind::kVector, std::move(name), 16u, true, false),
        element_type(element_type),
        max_count(max_count),
        element_size(element_size),
        nullability(nullability) {}

  const Type* const element_type;
  const uint32_t max_count;
  const uint32_t element_size;
  const types::Nullability nullability;
};

}  // namespace coded
}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_CODED_AST_H_
