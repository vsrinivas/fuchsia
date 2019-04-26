// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TABLES_GENERATOR_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TABLES_GENERATOR_H_

#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "coded_ast.h"
#include "coded_types_generator.h"
#include "flat_ast.h"

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
    explicit TablesGenerator(const flat::Library* library)
        : coded_types_generator_(library) {}

    ~TablesGenerator() = default;

    std::ostringstream Produce();

private:
    void GenerateInclude(std::string_view filename);
    void GenerateFilePreamble();
    void GenerateFilePostamble();

    template <typename Collection>
    void GenerateArray(const Collection& collection);

    void Generate(const coded::StructType& struct_type);
    void Generate(const coded::TableType& table_type);
    void Generate(const coded::UnionType& union_type);
    void Generate(const coded::XUnionType& xunion_type);
    void Generate(const coded::PointerType& pointer);
    void Generate(const coded::MessageType& message_type);
    void Generate(const coded::HandleType& handle_type);
    void Generate(const coded::InterfaceHandleType& interface_type);
    void Generate(const coded::RequestHandleType& request_type);
    void Generate(const coded::ArrayType& array_type);
    void Generate(const coded::StringType& string_type);
    void Generate(const coded::VectorType& vector_type);

    void Generate(const coded::Type* type);
    void Generate(const coded::StructField& field);
    void Generate(const coded::TableField& field);
    void Generate(const coded::XUnionField& field);

    void GenerateForward(const coded::StructType& struct_type);
    void GenerateForward(const coded::TableType& table_type);
    void GenerateForward(const coded::UnionType& union_type);
    void GenerateForward(const coded::XUnionType& xunion_type);

    CodedTypesGenerator coded_types_generator_;

    std::ostringstream tables_file_;
    size_t indent_level_ = 0u;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TABLES_GENERATOR_H_
