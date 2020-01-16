// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_CODED_TYPES_GENERATOR_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_CODED_TYPES_GENERATOR_H_

#include <map>
#include <string>
#include <vector>

#include "coded_ast.h"
#include "flat_ast.h"

namespace fidl {

class CodedTypesGenerator {
 public:
  explicit CodedTypesGenerator(const flat::Library* library) : library_(library) {}

  void CompileCodedTypes(const WireFormat wire_format);

  const flat::Library* library() const { return library_; }
  const std::vector<std::unique_ptr<coded::Type>>& coded_types() const { return coded_types_; }

  const coded::Type* CodedTypeFor(const flat::Name* name) const {
    auto it = named_coded_types_.find(name);
    return it != named_coded_types_.end() ? it->second.get() : nullptr;
  }

  std::vector<const coded::Type*> AllCodedTypes() const;

 private:
  // Returns a pointer owned by coded_types_.
  const coded::Type* CompileType(const flat::Type* type, coded::CodingContext context,
                                 const WireFormat wire_format);
  void CompileFields(const flat::Decl* decl, const WireFormat wire_format);
  void CompileDecl(const flat::Decl* decl, const WireFormat wire_format);
  void CompileXRef(const coded::Type* type, const WireFormat wire_format);

  const flat::Library* library_;

  template <typename FlatType, typename CodedType>
  using TypeMap = std::map<const FlatType*, const CodedType*, flat::PtrCompare<const FlatType>>;

  template <typename T, typename P = const T*>
  struct MaybeCodedTypeCompare {
    bool operator()(const std::pair<bool, P>& lhs, const std::pair<bool, P>& rhs) const {
      flat::PtrCompare<T> comparator;
      bool a_less_b = comparator(lhs.second, rhs.second);
      bool b_less_a = comparator(rhs.second, lhs.second);
      if (!a_less_b && !b_less_a) {
        // types are equivalent
        return lhs.first < rhs.first;
      } else {
        return a_less_b;
      }
    }
  };

  template <typename FlatType, typename CodedType>
  using MaybeCodedTypeMap = std::map<const std::pair<bool, const FlatType*>, const CodedType*,
                                     MaybeCodedTypeCompare<FlatType>>;

  // All flat::Types and flat::Names here are owned by library_, and
  // all coded::Types by the named_coded_types_ map or the coded_types_ vector.
  MaybeCodedTypeMap<flat::PrimitiveType, coded::PrimitiveType> primitive_type_map_;
  TypeMap<flat::HandleType, coded::HandleType> handle_type_map_;
  TypeMap<flat::RequestHandleType, coded::RequestHandleType> request_type_map_;
  TypeMap<flat::IdentifierType, coded::ProtocolHandleType> protocol_type_map_;
  MaybeCodedTypeMap<flat::ArrayType, coded::ArrayType> array_type_map_;
  TypeMap<flat::VectorType, coded::VectorType> vector_type_map_;
  TypeMap<flat::StringType, coded::StringType> string_type_map_;
  TypeMap<flat::IdentifierType, coded::XUnionType> xunion_type_map_;
  TypeMap<flat::IdentifierType, coded::StructPointerType> struct_type_map_;
  TypeMap<flat::IdentifierType, coded::UnionPointerType> union_type_map_;

  std::map<const flat::Name*, std::unique_ptr<coded::Type>, flat::PtrCompare<flat::Name>>
      named_coded_types_;
  std::vector<std::unique_ptr<coded::Type>> coded_types_;
};

}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_CODED_TYPES_GENERATOR_H_
