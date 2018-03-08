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

    template <typename Collection>
    void GenerateArray(const Collection& collection);

    void Generate(const coded::StructType& struct_type);
    void Generate(const coded::UnionType& union_type);
    void Generate(const coded::HandleType& handle_type);
    void Generate(const coded::ArrayType& array_type);
    void Generate(const coded::StringType& string_type);
    void Generate(const coded::VectorType& vector_type);

    void Generate(const coded::Type* type);
    void Generate(const coded::Field& field);

    // TODO(TO-856) The |name| is plumbed through as part of a hack to
    // work around not generating identical anonymous types once.
    const coded::Type* LookupType(const flat::Type* type, StringView name);

    // TODO(TO-856) A coded::Type is returned as part of a hack to
    // work around not generating identical anonymous types once.
    const coded::Type* Compile(const flat::Type* type, StringView name);
    void Compile(const flat::Decl* decl);

    const flat::Library* library_;

    // All flat::Types and flat::Names here are owned by library_, and
    // all coded::Types by the various coded_foo_types_ vectors.
    std::map<const flat::Name*, const coded::Type*, flat::NamePtrCompare> named_type_map_;

    std::vector<std::unique_ptr<coded::StructType>> coded_struct_types_;
    std::vector<std::unique_ptr<coded::UnionType>> coded_union_types_;
    std::vector<std::unique_ptr<coded::HandleType>> coded_handle_types_;
    std::vector<std::unique_ptr<coded::HandleType>> coded_request_types_;
    std::vector<std::unique_ptr<coded::ArrayType>> coded_array_types_;
    std::vector<std::unique_ptr<coded::StringType>> coded_string_types_;
    std::vector<std::unique_ptr<coded::VectorType>> coded_vector_types_;

    std::ostringstream tables_file_;
    size_t indent_level_ = 0u;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TABLES_GENERATOR_H_
