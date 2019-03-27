// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_CODED_TYPES_GENERATOR_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_CODED_TYPES_GENERATOR_H_

#include <map>
#include <string>
#include <vector>

#include "coded_ast.h"
#include "flat_ast.h"

namespace fidl {

class CodedTypesGenerator {
public:
    explicit CodedTypesGenerator(const flat::Library* library)
        : library_(library) {}

    void CompileCodedTypes();

    template <typename FlatType, typename CodedType>
    using TypeMap = std::map<const FlatType*, const CodedType*, flat::PtrCompare<FlatType>>;

    const flat::Library* library() const {return library_; }
    const std::vector<std::unique_ptr<coded::Type>>& coded_types() const { return coded_types_; }

    const coded::Type* CodedTypeFor(const flat::Name* name) {
        return named_coded_types_[name].get();
    }

private:
    // Returns a pointer owned by coded_types_.
    const coded::Type* CompileType(const flat::Type* type);
    void CompileFields(const flat::Decl* decl);
    void CompileDecl(const flat::Decl* decl);

    const flat::Library* library_;

    // All flat::Types and flat::Names here are owned by library_, and
    // all coded::Types by the named_coded_types_ map or the coded_types_ vector.
    TypeMap<flat::PrimitiveType, coded::PrimitiveType> primitive_type_map_;
    TypeMap<flat::HandleType, coded::HandleType> handle_type_map_;
    TypeMap<flat::RequestHandleType, coded::RequestHandleType> request_type_map_;
    TypeMap<flat::IdentifierType, coded::InterfaceHandleType> interface_type_map_;
    TypeMap<flat::ArrayType, coded::ArrayType> array_type_map_;
    TypeMap<flat::VectorType, coded::VectorType> vector_type_map_;
    TypeMap<flat::StringType, coded::StringType> string_type_map_;

    std::map<const flat::Name*, std::unique_ptr<coded::Type>, flat::PtrCompare<flat::Name>>
        named_coded_types_;
    std::vector<std::unique_ptr<coded::Type>> coded_types_;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_CODED_TYPES_GENERATOR_H_
