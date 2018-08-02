// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_JSON_GENERATOR_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_JSON_GENERATOR_H_

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "flat_ast.h"
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
    explicit JSONGenerator(const flat::Library* library)
        : library_(library) {}

    ~JSONGenerator() = default;

    std::ostringstream Produce();

private:
    enum class Position {
        kFirst,
        kSubsequent,
    };

    void GenerateEOF();

    template <typename Iterator>
    void GenerateArray(Iterator begin, Iterator end);

    template <typename Collection>
    void GenerateArray(const Collection& collection);

    void GenerateObjectPunctuation(Position position);

    template <typename Callback>
    void GenerateObject(Callback callback);

    template <typename Type>
    void GenerateObjectMember(StringView key, const Type& value,
                              Position position = Position::kSubsequent);

    template <typename T>
    void Generate(const std::unique_ptr<T>& value);

    void Generate(const flat::Decl* decl);

    template <typename T>
    void Generate(const std::vector<T>& value);

    void Generate(bool value);
    void Generate(StringView value);
    void Generate(SourceLocation value);
    void Generate(uint32_t value);

    void Generate(types::HandleSubtype value);
    void Generate(types::Nullability value);
    void Generate(types::PrimitiveSubtype value);

    void Generate(const raw::Identifier& value);
    void Generate(const raw::Literal& value);
    void Generate(const raw::Type& value);
    void Generate(const raw::Attribute& value);
    void Generate(const raw::AttributeList& value);

    void Generate(const flat::Ordinal& value);
    void Generate(const flat::Name& value);
    void Generate(const flat::Type& value);
    void Generate(const flat::Constant& value);
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
    void Generate(const flat::Library* library);

    void GenerateDeclarationsEntry(int count, const flat::Name& name, StringView decl);
    void GenerateDeclarationsMember(const flat::Library* library,
                                    Position position = Position::kSubsequent);

    const flat::Library* library_;
    int indent_level_;
    std::ostringstream json_file_;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_JSON_GENERATOR_H_
