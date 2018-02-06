// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_JSON_GENERATOR_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_JSON_GENERATOR_H_

#include <sstream>
#include <string>
#include <vector>
#include <memory>

#include "coded_ast.h"
#include "flat_ast.h"
#include "library.h"
#include "string_view.h"

namespace fidl {

// Methods or functions named "Emit..." are the actual interface to
// the JSON output.

// Methods named "Generate..." directly generate JSON output via the
// "Emit" routines.

// Methods named "Produce..." indirectly generate JSON output by calling
// the Generate methods, and should not call the "Emit" functions
// directly.

class JSONGenerator {
public:
    explicit JSONGenerator(Library* library) : library_(library) {}

    ~JSONGenerator() = default;

    void ProduceJSON(std::ostringstream* json_file_out);

 private:
    enum class Position {
        First,
        Subsequent,
    };

    template<typename Collection>
    void GenerateArray(const Collection& collection);

    template<typename Callback>
    void GenerateObject(Callback callback);

    template<typename Type>
    void GenerateObjectMember(StringView key, const Type& value,
                              Position position = Position::Subsequent);

    template<typename T>
    void Generate(const std::unique_ptr<T>& value);

    template<typename T>
    void Generate(const std::vector<T>& value);

    void Generate(bool value);
    void Generate(StringView value);

    void Generate(types::HandleSubtype value);

    void Generate(ast::Nullability value);
    void Generate(ast::PrimitiveType::Subtype value);
    void Generate(const ast::Identifier& value);
    void Generate(const ast::CompoundIdentifier& value);
    void Generate(const ast::Literal& value);
    void Generate(const ast::Type& value);
    void Generate(const ast::Constant& value);

    void Generate(const flat::Ordinal& value);
    void Generate(const flat::Name& value);
    void Generate(const flat::Const& value);
    void Generate(const flat::Enum& value);
    void Generate(const flat::Enum::Member& value);
    void Generate(const flat::Interface& value);
    void Generate(const flat::Interface::Method& value);
    void Generate(const flat::Interface::Method::Parameter& value);
    void Generate(const flat::Struct& value);
    void Generate(const flat::Struct::Member& value);
    void Generate(const flat::Union& value);
    void Generate(const flat::Union::Member& value);

    Library* library_;
    int indent_level_;
    std::ostringstream json_file_;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_C_GENERATOR_H_
