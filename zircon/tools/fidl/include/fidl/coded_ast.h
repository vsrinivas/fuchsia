// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_CODED_AST_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_CODED_AST_H_

#include <stdint.h>

#include <cassert>
#include <memory>
#include <string>
#include <vector>

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
  StructField(const Type* type, uint32_t size, uint32_t offset, uint32_t padding,
              const Type* struct_type, uint32_t field_num)
      : type(type),
        size(size),
        offset(offset),
        padding(padding),
        struct_type(struct_type),
        field_num(field_num) {}

  const Type* type;
  const uint32_t size;
  const uint32_t offset;
  const uint32_t padding;
  // TODO(fxb/7704): When we remove special handling of requests/responses, this
  // needs to be a StructType (we'll remove MessageType).
  const Type* struct_type;
  // The field number in the FIDL struct, first field is numbered 0. Since we
  // omit generation for certain types of fields (e.g. primitives), this number
  // does not correspond to the field index in the coded struct table, and these
  // must be manually mapped.
  const uint32_t field_num;
};

struct UnionField {
  UnionField(const Type* type, uint32_t padding, uint32_t xunion_ordinal)
      : type(type), padding(padding), xunion_ordinal(xunion_ordinal) {}

  const Type* type;
  const uint32_t padding;
  const uint32_t xunion_ordinal;
};

struct TableField {
  TableField(const Type* type, uint32_t ordinal) : type(type), ordinal(ordinal) {}

  const Type* type;
  const uint32_t ordinal;
};

struct XUnionField {
  XUnionField(const Type* type, uint32_t ordinal, uint32_t hashed_ordinal,
              uint32_t explicit_ordinal)
      : type(type),
        ordinal(ordinal),
        hashed_ordinal(hashed_ordinal),
        explicit_ordinal(explicit_ordinal) {}

  const Type* type;
  const uint32_t ordinal;
  const uint32_t hashed_ordinal;
  const uint32_t explicit_ordinal;
};

struct Type {
  virtual ~Type() = default;

  enum struct Kind {
    kPrimitive,
    kEnum,
    kBits,
    kHandle,
    kProtocolHandle,
    kRequestHandle,
    kStruct,
    kTable,
    kUnion,
    kXUnion,
    kStructPointer,
    kUnionPointer,
    kMessage,
    kProtocol,
    kArray,
    kString,
    kVector,
  };

  Type(Kind kind, std::string coded_name, uint32_t size, bool coding_needed)
      : kind(kind), coded_name(std::move(coded_name)), size(size), coding_needed(coding_needed) {}

  const Kind kind;
  const std::string coded_name;
  uint32_t size;
  const bool coding_needed;
};

struct PrimitiveType : public Type {
  PrimitiveType(std::string name, types::PrimitiveSubtype subtype, uint32_t size,
                CodingContext context)
      : Type(Kind::kPrimitive, std::move(name), size, context == CodingContext::kInsideEnvelope),
        subtype(subtype) {}

  const types::PrimitiveSubtype subtype;
};

struct EnumType : public Type {
  EnumType(std::string name, types::PrimitiveSubtype subtype, uint32_t size,
           std::vector<uint64_t> members, std::string qname)
      : Type(Kind::kEnum, std::move(name), size, true),
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
      : Type(Kind::kBits, std::move(name), size, true),
        subtype(subtype),
        mask(mask),
        qname(std::move(qname)) {}

  const types::PrimitiveSubtype subtype;
  const uint64_t mask;
  const std::string qname;
};

struct HandleType : public Type {
  HandleType(std::string name, types::HandleSubtype subtype, types::Nullability nullability)
      : Type(Kind::kHandle, std::move(name), 4u, true),
        subtype(subtype),
        nullability(nullability) {}

  const types::HandleSubtype subtype;
  const types::Nullability nullability;
};

struct ProtocolHandleType : public Type {
  ProtocolHandleType(std::string name, types::Nullability nullability)
      : Type(Kind::kProtocolHandle, std::move(name), 4u, true), nullability(nullability) {}

  const types::Nullability nullability;
};

struct RequestHandleType : public Type {
  RequestHandleType(std::string name, types::Nullability nullability)
      : Type(Kind::kRequestHandle, std::move(name), 4u, true), nullability(nullability) {}

  const types::Nullability nullability;
};

struct StructPointerType;

struct StructType : public Type {
  StructType(std::string name, std::vector<StructField> fields, uint32_t size,
             uint32_t max_out_of_line, bool contains_union, std::string qname)
      : Type(Kind::kStruct, std::move(name), size, true),
        max_out_of_line(max_out_of_line),
        contains_union(contains_union),
        fields(std::move(fields)),
        qname(std::move(qname)) {}

  uint32_t max_out_of_line;
  bool contains_union;
  std::vector<StructField> fields;
  std::string qname;
  StructPointerType* maybe_reference_type = nullptr;
};

struct StructPointerType : public Type {
  StructPointerType(std::string name, const Type* type, const uint32_t pointer_size)
      : Type(Kind::kStructPointer, std::move(name), pointer_size, true),
        element_type(static_cast<const StructType*>(type)) {
    assert(type->kind == Type::Kind::kStruct);
  }

  const StructType* element_type;
};

struct UnionPointerType;

struct UnionType : public Type {
  UnionType(std::string name, std::vector<UnionField> members, uint32_t data_offset, uint32_t size,
            std::string qname)
      : Type(Kind::kUnion, std::move(name), size, true),
        members(std::move(members)),
        data_offset(data_offset),
        qname(std::move(qname)) {}

  std::vector<UnionField> members;
  const uint32_t data_offset;
  std::string qname;
  UnionPointerType* maybe_reference_type = nullptr;
};

struct UnionPointerType : public Type {
  UnionPointerType(std::string name, const Type* type, const uint32_t pointer_size)
      : Type(Kind::kUnionPointer, std::move(name), pointer_size, true),
        element_type(static_cast<const UnionType*>(type)) {
    assert(type->kind == Type::Kind::kUnion);
  }

  const UnionType* element_type;
};

struct TableType : public Type {
  TableType(std::string name, std::vector<TableField> fields, uint32_t size, std::string qname)
      : Type(Kind::kTable, std::move(name), size, true),
        fields(std::move(fields)),
        qname(std::move(qname)) {}

  std::vector<TableField> fields;
  std::string qname;
};

struct XUnionType : public Type {
  XUnionType(std::string name, std::vector<XUnionField> fields, std::string qname,
             types::Nullability nullability, types::Strictness strictness)
      : Type(Kind::kXUnion, std::move(name), 24u, true),
        fields(std::move(fields)),
        qname(std::move(qname)),
        nullability(nullability),
        strictness(strictness) {}

  std::vector<XUnionField> fields;
  const std::string qname;
  types::Nullability nullability;
  types::Strictness strictness;
  XUnionType* maybe_reference_type = nullptr;

  // This XUnionType was created from...
  enum class Source {
    kFromXUnion = 1,    // ... a xunion declaration.
    kFromUnion,         // ... a (static) union declaration.
    kFromUnionPointer,  // ... a (static) pointer-to-union declaration.
  };
  Source source = Source::kFromXUnion;

  bool FromUnion() const { return source == Source::kFromUnion; }

  bool FromUnionPointer() const { return source == Source::kFromUnionPointer; }
};

struct MessageType : public Type {
  MessageType(std::string name, std::vector<StructField> fields, uint32_t size,
              uint32_t max_out_of_line, bool contains_union, std::string qname)
      : Type(Kind::kMessage, std::move(name), size, true),
        max_out_of_line(max_out_of_line),
        contains_union(contains_union),
        fields(std::move(fields)),
        qname(std::move(qname)) {}

  uint32_t max_out_of_line;
  bool contains_union;
  std::vector<StructField> fields;
  std::string qname;
};

struct ProtocolType : public Type {
  explicit ProtocolType(std::vector<std::unique_ptr<MessageType>> messages_during_compile)
      // N.B. ProtocolTypes are never used in the eventual coding table generation.
      : Type(Kind::kProtocol, "", 0, false),
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
      : Type(Kind::kArray, std::move(name), array_size,
             context == CodingContext::kInsideEnvelope || element_type->coding_needed),
        element_type(element_type),
        element_size(element_size) {}

  const Type* const element_type;
  const uint32_t element_size;
};

struct StringType : public Type {
  StringType(std::string name, uint32_t max_size, types::Nullability nullability)
      : Type(Kind::kString, std::move(name), 16u, true),
        max_size(max_size),
        nullability(nullability) {}

  const uint32_t max_size;
  const types::Nullability nullability;
};

struct VectorType : public Type {
  VectorType(std::string name, const Type* element_type, uint32_t max_count, uint32_t element_size,
             types::Nullability nullability)
      : Type(Kind::kVector, std::move(name), 16u, true),
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
