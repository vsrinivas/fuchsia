// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/json_generator.h"

#include "fidl/names.h"

namespace fidl {

namespace {

constexpr const char* kIndent = "  ";

// Functions named "Emit..." are called to actually emit to an std::ostream
// is here. No other functions should directly emit to the streams.

void EmitBoolean(std::ostream* file, bool value) {
    if (value)
        *file << "true";
    else
        *file << "false";
}

void EmitString(std::ostream* file, StringView value) {
    *file << "\"";
    for (size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        switch (c) {
        case '"':
            *file << "\\\"";
            break;
        case '\\':
            *file << "\\\\";
            break;
        // TODO(TO-824): Escape more characters.
        default:
            *file << c;
            break;
        }
    }
    *file << "\"";
}

void EmitLiteral(std::ostream* file, StringView value) {
    file->rdbuf()->sputn(value.data(), value.size());
}

void EmitUint32(std::ostream* file, uint32_t value) {
    *file << value;
}

void EmitNewline(std::ostream* file) {
    *file << "\n";
}

void EmitNewlineAndIndent(std::ostream* file, int indent_level) {
    *file << "\n";
    while (indent_level--)
        *file << kIndent;
}

void EmitObjectBegin(std::ostream* file) {
    *file << "{";
}

void EmitObjectSeparator(std::ostream* file, int indent_level) {
    *file << ",";
    EmitNewlineAndIndent(file, indent_level);
}

void EmitObjectEnd(std::ostream* file) {
    *file << "}";
}

void EmitObjectKey(std::ostream* file, int indent_level, StringView key) {
    EmitString(file, key);
    *file << ": ";
}

void EmitArrayBegin(std::ostream* file) {
    *file << "[";
}

void EmitArraySeparator(std::ostream* file, int indent_level) {
    *file << ",";
    EmitNewlineAndIndent(file, indent_level);
}

void EmitArrayEnd(std::ostream* file) {
    *file << "]";
}

} // namespace

void JSONGenerator::GenerateEOF() {
    EmitNewline(&json_file_);
}

template <typename Iterator>
void JSONGenerator::GenerateArray(Iterator begin, Iterator end) {
    EmitArrayBegin(&json_file_);

    if (begin != end)
        EmitNewlineAndIndent(&json_file_, ++indent_level_);

    for (Iterator it = begin; it != end; ++it) {
        if (it != begin)
            EmitArraySeparator(&json_file_, indent_level_);
        Generate(*it);
    }

    if (begin != end)
        EmitNewlineAndIndent(&json_file_, --indent_level_);

    EmitArrayEnd(&json_file_);
}

template <typename Collection>
void JSONGenerator::GenerateArray(const Collection& collection) {
    GenerateArray(collection.begin(), collection.end());
}

template <typename Callback>
void JSONGenerator::GenerateObject(Callback callback) {
    int original_indent_level = indent_level_;

    EmitObjectBegin(&json_file_);

    callback();

    if (indent_level_ > original_indent_level)
        EmitNewlineAndIndent(&json_file_, --indent_level_);

    EmitObjectEnd(&json_file_);
}

void JSONGenerator::GenerateObjectPunctuation(Position position) {
    switch (position) {
    case Position::kFirst:
        EmitNewlineAndIndent(&json_file_, ++indent_level_);
        break;
    case Position::kSubsequent:
        EmitObjectSeparator(&json_file_, indent_level_);
        break;
    }
}

template <typename Type>
void JSONGenerator::GenerateObjectMember(StringView key, const Type& value, Position position) {
    GenerateObjectPunctuation(position);
    EmitObjectKey(&json_file_, indent_level_, key);
    Generate(value);
}

void JSONGenerator::Generate(const flat::Decl* decl) {
    Generate(decl->name);
}

template <typename T>
void JSONGenerator::Generate(const std::unique_ptr<T>& value) {
    Generate(*value);
}

template <typename T>
void JSONGenerator::Generate(const std::vector<T>& value) {
    GenerateArray(value);
}

void JSONGenerator::Generate(bool value) {
    EmitBoolean(&json_file_, value);
}

void JSONGenerator::Generate(StringView value) {
    EmitString(&json_file_, value);
}

void JSONGenerator::Generate(SourceLocation value) {
    EmitString(&json_file_, value.data());
}

void JSONGenerator::Generate(uint32_t value) {
    EmitUint32(&json_file_, value);
}

void JSONGenerator::Generate(types::HandleSubtype value) {
    EmitString(&json_file_, NameHandleSubtype(value));
}

void JSONGenerator::Generate(types::Nullability value) {
    switch (value) {
    case types::Nullability::kNullable:
        EmitBoolean(&json_file_, true);
        break;
    case types::Nullability::kNonnullable:
        EmitBoolean(&json_file_, false);
        break;
    }
}

void JSONGenerator::Generate(types::PrimitiveSubtype value) {
    EmitString(&json_file_, NamePrimitiveSubtype(value));
}

void JSONGenerator::Generate(const raw::Identifier& value) {
    EmitString(&json_file_, value.location().data());
}

void JSONGenerator::Generate(const raw::Literal& value) {
    GenerateObject([&]() {
        GenerateObjectMember("kind", NameRawLiteralKind(value.kind), Position::kFirst);

        switch (value.kind) {
        case raw::Literal::Kind::kString: {
            auto type = static_cast<const raw::StringLiteral*>(&value);
            EmitObjectSeparator(&json_file_, indent_level_);
            EmitObjectKey(&json_file_, indent_level_, "value");
            EmitLiteral(&json_file_, type->location().data());
            break;
        }
        case raw::Literal::Kind::kNumeric: {
            auto type = static_cast<const raw::NumericLiteral*>(&value);
            GenerateObjectMember("value", type->location().data());
            break;
        }
        case raw::Literal::Kind::kTrue: {
            break;
        }
        case raw::Literal::Kind::kFalse: {
            break;
        }
        }
    });
}

void JSONGenerator::Generate(const flat::Constant& value) {
    GenerateObject([&]() {
        GenerateObjectMember("kind", NameFlatConstantKind(value.kind), Position::kFirst);

        switch (value.kind) {
        case flat::Constant::Kind::kIdentifier: {
            auto type = static_cast<const flat::IdentifierConstant*>(&value);
            GenerateObjectMember("identifier", type->name);
            break;
        }
        case flat::Constant::Kind::kLiteral: {
            auto type = static_cast<const flat::LiteralConstant*>(&value);
            GenerateObjectMember("literal", type->literal);
            break;
        }
        }
    });
}

void JSONGenerator::Generate(const flat::Type& value) {
    GenerateObject([&]() {
        GenerateObjectMember("kind", NameFlatTypeKind(value.kind), Position::kFirst);

        switch (value.kind) {
        case flat::Type::Kind::kArray: {
            auto type = static_cast<const flat::ArrayType*>(&value);
            GenerateObjectMember("element_type", type->element_type);
            GenerateObjectMember("element_count", type->element_count.Value());
            break;
        }
        case flat::Type::Kind::kVector: {
            auto type = static_cast<const flat::VectorType*>(&value);
            GenerateObjectMember("element_type", type->element_type);
            if (type->element_count.Value() < flat::Size::Max().Value())
                GenerateObjectMember("maybe_element_count", type->element_count.Value());
            GenerateObjectMember("nullable", type->nullability);
            break;
        }
        case flat::Type::Kind::kString: {
            auto type = static_cast<const flat::StringType*>(&value);
            if (type->max_size.Value() < flat::Size::Max().Value())
                GenerateObjectMember("maybe_element_count", type->max_size.Value());
            GenerateObjectMember("nullable", type->nullability);
            break;
        }
        case flat::Type::Kind::kHandle: {
            auto type = static_cast<const flat::HandleType*>(&value);
            GenerateObjectMember("subtype", type->subtype);
            GenerateObjectMember("nullable", type->nullability);
            break;
        }
        case flat::Type::Kind::kRequestHandle: {
            auto type = static_cast<const flat::RequestHandleType*>(&value);
            GenerateObjectMember("subtype", type->name);
            GenerateObjectMember("nullable", type->nullability);
            break;
        }
        case flat::Type::Kind::kPrimitive: {
            auto type = static_cast<const flat::PrimitiveType*>(&value);
            GenerateObjectMember("subtype", type->subtype);
            break;
        }
        case flat::Type::Kind::kIdentifier: {
            auto type = static_cast<const flat::IdentifierType*>(&value);
            GenerateObjectMember("identifier", type->name);
            GenerateObjectMember("nullable", type->nullability);
            break;
        }
        }
    });
}

void JSONGenerator::Generate(const raw::Attribute& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::kFirst);
        if (value.value)
            GenerateObjectMember("value", value.value->location());
        else
            GenerateObjectMember("value", StringView());
    });
}

void JSONGenerator::Generate(const raw::AttributeList& value) {
    Generate(value.attribute_list);
}

void JSONGenerator::Generate(const flat::Ordinal& value) {
    EmitUint32(&json_file_, value.Value());
}

void JSONGenerator::Generate(const flat::Name& value) {
    // These look like (when there is a library)
    //     { "LIB.LIB.LIB", "ID" }
    // or (when there is not)
    //     { "ID" }
    Generate(NameName(value, ".", "/"));
}

void JSONGenerator::Generate(const flat::Const& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::kFirst);
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        GenerateObjectMember("type", value.type);
        GenerateObjectMember("value", value.value);
    });
}

void JSONGenerator::Generate(const flat::Enum& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::kFirst);
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        GenerateObjectMember("type", value.type);
        GenerateObjectMember("members", value.members);
    });
}

void JSONGenerator::Generate(const flat::Enum::Member& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::kFirst);
        GenerateObjectMember("value", value.value);
    });
}

void JSONGenerator::Generate(const flat::Interface& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::kFirst);
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        GenerateObjectMember("methods", value.all_methods);
    });
}

void JSONGenerator::Generate(const flat::Interface::Method* method) {
    assert(method != nullptr);
    const auto& value = *method;
    GenerateObject([&]() {
        GenerateObjectMember("ordinal", value.ordinal, Position::kFirst);
        GenerateObjectMember("name", value.name);
        GenerateObjectMember("has_request", value.maybe_request != nullptr);
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        if (value.maybe_request != nullptr) {
            GenerateObjectMember("maybe_request", value.maybe_request->parameters);
            GenerateObjectMember("maybe_request_size", value.maybe_request->typeshape.Size());
            GenerateObjectMember("maybe_request_alignment",
                                 value.maybe_request->typeshape.Alignment());
        }
        GenerateObjectMember("has_response", value.maybe_response != nullptr);
        if (value.maybe_response != nullptr) {
            GenerateObjectMember("maybe_response", value.maybe_response->parameters);
            GenerateObjectMember("maybe_response_size", value.maybe_response->typeshape.Size());
            GenerateObjectMember("maybe_response_alignment",
                                 value.maybe_response->typeshape.Alignment());
        }
    });
}

void JSONGenerator::Generate(const flat::Interface::Method::Parameter& value) {
    GenerateObject([&]() {
        GenerateObjectMember("type", value.type, Position::kFirst);
        GenerateObjectMember("name", value.name);
        GenerateObjectMember("size", value.fieldshape.Size());
        GenerateObjectMember("alignment", value.fieldshape.Alignment());
        GenerateObjectMember("offset", value.fieldshape.Offset());
    });
}

void JSONGenerator::Generate(const flat::Struct& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::kFirst);
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        GenerateObjectMember("members", value.members);
        GenerateObjectMember("size", value.typeshape.Size());
        GenerateObjectMember("alignment", value.typeshape.Alignment());
        GenerateObjectMember("max_handles", value.typeshape.MaxHandles());
    });
}

void JSONGenerator::Generate(const flat::Struct::Member& value) {
    GenerateObject([&]() {
        GenerateObjectMember("type", value.type, Position::kFirst);
        GenerateObjectMember("name", value.name);
        if (value.maybe_default_value)
            GenerateObjectMember("maybe_default_value", value.maybe_default_value);
        GenerateObjectMember("size", value.fieldshape.Size());
        GenerateObjectMember("alignment", value.fieldshape.Alignment());
        GenerateObjectMember("offset", value.fieldshape.Offset());
        GenerateObjectMember("max_handles", value.fieldshape.MaxHandles());
    });
}

void JSONGenerator::Generate(const flat::Union& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::kFirst);
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        GenerateObjectMember("members", value.members);
        GenerateObjectMember("size", value.typeshape.Size());
        GenerateObjectMember("alignment", value.typeshape.Alignment());
        GenerateObjectMember("max_handles", value.typeshape.MaxHandles());
    });
}

void JSONGenerator::Generate(const flat::Union::Member& value) {
    GenerateObject([&]() {
        GenerateObjectMember("type", value.type, Position::kFirst);
        GenerateObjectMember("name", value.name);
        GenerateObjectMember("size", value.fieldshape.Size());
        GenerateObjectMember("alignment", value.fieldshape.Alignment());
        GenerateObjectMember("offset", value.fieldshape.Offset());
    });
}

void JSONGenerator::Generate(const flat::Library* library) {
    GenerateObject([&]() {
        auto library_name = flat::LibraryName(library, ".");
        GenerateObjectMember("name", library_name, Position::kFirst);
        GenerateDeclarationsMember(library);
    });
}

void JSONGenerator::GenerateDeclarationsEntry(int count, const flat::Name& name, StringView decl) {
    if (count == 0)
        EmitNewlineAndIndent(&json_file_, ++indent_level_);
    else
        EmitObjectSeparator(&json_file_, indent_level_);
    EmitObjectKey(&json_file_, indent_level_, NameName(name, ".", "/"));
    EmitString(&json_file_, decl);
}

void JSONGenerator::GenerateDeclarationsMember(const flat::Library* library, Position position) {
    GenerateObjectPunctuation(position);
    EmitObjectKey(&json_file_, indent_level_, "declarations");
    GenerateObject([&]() {
        int count = 0;
        for (const auto& decl : library->const_declarations_)
            GenerateDeclarationsEntry(count++, decl->name, "const");

        for (const auto& decl : library->enum_declarations_)
            GenerateDeclarationsEntry(count++, decl->name, "enum");

        for (const auto& decl : library->interface_declarations_)
            GenerateDeclarationsEntry(count++, decl->name, "interface");

        for (const auto& decl : library->struct_declarations_)
            GenerateDeclarationsEntry(count++, decl->name, "struct");

        for (const auto& decl : library->union_declarations_)
            GenerateDeclarationsEntry(count++, decl->name, "union");
    });
}

std::ostringstream JSONGenerator::Produce() {
    indent_level_ = 0;
    GenerateObject([&]() {
        GenerateObjectMember("version", StringView("0.0.1"), Position::kFirst);

        GenerateObjectMember("name", LibraryName(library_, "."));

        GenerateObjectPunctuation(Position::kSubsequent);
        EmitObjectKey(&json_file_, indent_level_, "library_dependencies");
        std::vector<flat::Library*> dependencies;
        for (const auto& entry : *library_->dependencies_) {
            if (entry.second->HasAttribute("Internal"))
                continue;
            dependencies.push_back(entry.second.get());
        }

        GenerateArray(dependencies.begin(), dependencies.end());

        GenerateObjectMember("const_declarations", library_->const_declarations_);
        GenerateObjectMember("enum_declarations", library_->enum_declarations_);
        GenerateObjectMember("interface_declarations", library_->interface_declarations_);
        GenerateObjectMember("struct_declarations", library_->struct_declarations_);
        GenerateObjectMember("union_declarations", library_->union_declarations_);

        // The library's declaration_order_ contains all the declarations for all
        // transitive dependencies. The backend only needs the declaration order
        // for this specific library.
        std::vector<std::string> declaration_order;
        for (flat::Decl* decl : library_->declaration_order_) {
            if (decl->name.library() == library_)
                declaration_order.push_back(NameName(decl->name, ".", "/"));
        }
        GenerateObjectMember("declaration_order", declaration_order);

        GenerateDeclarationsMember(library_);
    });
    GenerateEOF();

    return std::move(json_file_);
}

} // namespace fidl
