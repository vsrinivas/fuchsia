// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TABLES_GENERATOR_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TABLES_GENERATOR_H_

#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "coded_ast.h"
#include "flat_ast.h"
#include "string_view.h"

namespace fidl {

// Methods or functions named "Emit..." are the actual interface to
// the tables output.

// Methods named "Generate..." directly generate tables output via the
// "Emit" routines.

// Methods named "Produce..." indirectly generate tables output by calling
// the Generate methods, and should not call the "Emit" functions
// directly.

class TablesGenerator {
public:
    explicit TablesGenerator(const flat::Library* library) : library_(library) {}

    ~TablesGenerator() = default;

    std::ostringstream Produce();

private:
    void GenerateInclude(StringView filename);
    void GenerateFilePreamble();
    void GenerateFilePostamble();

    template <typename Collection> void GenerateArray(const Collection& collection);

    void Generate(const coded::StructType& struct_type);
    void Generate(const coded::UnionType& union_type);
    void Generate(const coded::MessageType& message_type);
    void Generate(const coded::HandleType& handle_type);
    void Generate(const coded::InterfaceHandleType& interface_type);
    void Generate(const coded::RequestHandleType& request_type);
    void Generate(const coded::ArrayType& array_type);
    void Generate(const coded::StringType& string_type);
    void Generate(const coded::VectorType& vector_type);

    void Generate(const coded::Type* type);
    void Generate(const coded::Field& field);

    void GeneratePointerIfNeeded(const coded::StructType& struct_type);
    void GeneratePointerIfNeeded(const coded::UnionType& union_type);

    void GenerateForward(const coded::StructType& struct_type);
    void GenerateForward(const coded::UnionType& union_type);

    // Returns a pointer owned by coded_types_.
    const coded::Type* CompileType(const flat::Type* type);
    void CompileFields(const flat::Decl* decl);
    void Compile(const flat::Decl* decl);

    const flat::Library* library_;

    // All flat::Types and flat::Names here are owned by library_, and
    // all coded::Types by the named_coded_types_ map or the coded_types_ vector.
    template <typename FlatType, typename CodedType>
    using TypeMap = std::map<const FlatType*, const CodedType*, flat::PtrCompare<FlatType>>;
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

    std::ostringstream tables_file_;
    size_t indent_level_ = 0u;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TABLES_GENERATOR_H_
