// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/json_generator.h"

namespace fidl {

namespace {

constexpr const char* kIndent = "  ";

std::string LongName(const flat::Name& name) {
    // TODO(TO-701) Handle complex names.
    return name.data();
}

std::string PrimitiveSubtypeName(types::PrimitiveSubtype subtype) {
    switch (subtype) {
    case types::PrimitiveSubtype::Int8:
        return "int8";
    case types::PrimitiveSubtype::Int16:
        return "int16";
    case types::PrimitiveSubtype::Int32:
        return "int32";
    case types::PrimitiveSubtype::Int64:
        return "int64";
    case types::PrimitiveSubtype::Uint8:
        return "uint8";
    case types::PrimitiveSubtype::Uint16:
        return "uint16";
    case types::PrimitiveSubtype::Uint32:
        return "uint32";
    case types::PrimitiveSubtype::Uint64:
        return "uint64";
    case types::PrimitiveSubtype::Bool:
        return "bool";
    case types::PrimitiveSubtype::Status:
        return "status";
    case types::PrimitiveSubtype::Float32:
        return "float32";
    case types::PrimitiveSubtype::Float64:
        return "float64";
    }
}

std::string HandleSubtypeName(types::HandleSubtype subtype) {
    switch (subtype) {
    case types::HandleSubtype::Handle:
        return "handle";
    case types::HandleSubtype::Process:
        return "process";
    case types::HandleSubtype::Thread:
        return "thread";
    case types::HandleSubtype::Vmo:
        return "vmo";
    case types::HandleSubtype::Channel:
        return "channel";
    case types::HandleSubtype::Event:
        return "event";
    case types::HandleSubtype::Port:
        return "port";
    case types::HandleSubtype::Interrupt:
        return "interrupt";
    case types::HandleSubtype::Log:
        return "log";
    case types::HandleSubtype::Socket:
        return "socket";
    case types::HandleSubtype::Resource:
        return "resource";
    case types::HandleSubtype::Eventpair:
        return "eventpair";
    case types::HandleSubtype::Job:
        return "job";
    case types::HandleSubtype::Vmar:
        return "vmar";
    case types::HandleSubtype::Fifo:
        return "fifo";
    case types::HandleSubtype::Guest:
        return "guest";
    case types::HandleSubtype::Timer:
        return "timer";
    }
}

std::string LiteralKindName(raw::Literal::Kind kind) {
    switch (kind) {
    case raw::Literal::Kind::String:
        return "string";
    case raw::Literal::Kind::Numeric:
        return "numeric";
    case raw::Literal::Kind::True:
        return "true";
    case raw::Literal::Kind::False:
        return "false";
    case raw::Literal::Kind::Default:
        return "default";
    }
}

std::string TypeKindName(flat::Type::Kind kind) {
    switch (kind) {
    case flat::Type::Kind::Array:
        return "array";
    case flat::Type::Kind::Vector:
        return "vector";
    case flat::Type::Kind::String:
        return "string";
    case flat::Type::Kind::Handle:
        return "handle";
    case flat::Type::Kind::Request:
        return "request";
    case flat::Type::Kind::Primitive:
        return "primitive";
    case flat::Type::Kind::Identifier:
        return "identifier";
    }
}

std::string ConstantKindName(raw::Constant::Kind kind) {
    switch (kind) {
    case raw::Constant::Kind::Identifier:
        return "identifier";
    case raw::Constant::Kind::Literal:
        return "literal";
    }
}

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

void EmitUint64(std::ostream* file, uint64_t value) {
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

template <typename Collection>
void JSONGenerator::GenerateArray(const Collection& collection) {
    EmitArrayBegin(&json_file_);

    if (!collection.empty())
        EmitNewlineAndIndent(&json_file_, ++indent_level_);

    for (size_t i = 0; i < collection.size(); ++i) {
        if (i)
            EmitArraySeparator(&json_file_, indent_level_);
        Generate(collection[i]);
    }

    if (!collection.empty())
        EmitNewlineAndIndent(&json_file_, --indent_level_);

    EmitArrayEnd(&json_file_);
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

template <typename Type>
void JSONGenerator::GenerateObjectMember(StringView key, const Type& value, Position position) {
    switch (position) {
    case Position::First:
        EmitNewlineAndIndent(&json_file_, ++indent_level_);
        break;
    case Position::Subsequent:
        EmitObjectSeparator(&json_file_, indent_level_);
        break;
    }
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

void JSONGenerator::Generate(uint64_t value) {
    EmitUint64(&json_file_, value);
}

void JSONGenerator::Generate(types::HandleSubtype value) {
    EmitString(&json_file_, HandleSubtypeName(value));
}

void JSONGenerator::Generate(types::Nullability value) {
    switch (value) {
    case types::Nullability::Nullable:
        EmitBoolean(&json_file_, true);
        break;
    case types::Nullability::Nonnullable:
        EmitBoolean(&json_file_, false);
        break;
    }
}

void JSONGenerator::Generate(types::PrimitiveSubtype value) {
    EmitString(&json_file_, PrimitiveSubtypeName(value));
}

void JSONGenerator::Generate(const raw::Identifier& value) {
    EmitString(&json_file_, value.location.data());
}

void JSONGenerator::Generate(const raw::CompoundIdentifier& value) {
    Generate(value.components);
}

void JSONGenerator::Generate(const raw::Literal& value) {
    GenerateObject([&]() {
        GenerateObjectMember("kind", LiteralKindName(value.kind), Position::First);

        switch (value.kind) {
        case raw::Literal::Kind::String: {
            auto type = static_cast<const raw::StringLiteral*>(&value);
            EmitObjectSeparator(&json_file_, indent_level_);
            EmitObjectKey(&json_file_, indent_level_, "value");
            EmitLiteral(&json_file_, type->location.data());
            break;
        }
        case raw::Literal::Kind::Numeric: {
            auto type = static_cast<const raw::NumericLiteral*>(&value);
            GenerateObjectMember("value", type->location.data());
            break;
        }
        case raw::Literal::Kind::True: {
            break;
        }
        case raw::Literal::Kind::False: {
            break;
        }
        case raw::Literal::Kind::Default: {
            break;
        }
        }
    });
}

void JSONGenerator::Generate(const flat::Type& value) {
    GenerateObject([&]() {
        GenerateObjectMember("kind", TypeKindName(value.kind), Position::First);

        switch (value.kind) {
        case flat::Type::Kind::Array: {
            auto type = static_cast<const flat::ArrayType*>(&value);
            GenerateObjectMember("element_type", type->element_type);
            GenerateObjectMember("element_count", type->element_count.Value());
            break;
        }
        case flat::Type::Kind::Vector: {
            auto type = static_cast<const flat::VectorType*>(&value);
            GenerateObjectMember("element_type", type->element_type);
            if (type->element_count.Value() < flat::Size::Max().Value())
                GenerateObjectMember("maybe_element_count", type->element_count.Value());
            GenerateObjectMember("nullable", type->nullability);
            break;
        }
        case flat::Type::Kind::String: {
            auto type = static_cast<const flat::StringType*>(&value);
            if (type->max_size.Value() < flat::Size::Max().Value())
                GenerateObjectMember("maybe_element_count", type->max_size.Value());
            GenerateObjectMember("nullable", type->nullability);
            break;
        }
        case flat::Type::Kind::Handle: {
            auto type = static_cast<const flat::HandleType*>(&value);
            GenerateObjectMember("subtype", type->subtype);
            GenerateObjectMember("nullable", type->nullability);
            break;
        }
        case flat::Type::Kind::Request: {
            auto type = static_cast<const flat::RequestType*>(&value);
            GenerateObjectMember("subtype", type->name);
            GenerateObjectMember("nullable", type->nullability);
            break;
        }
        case flat::Type::Kind::Primitive: {
            auto type = static_cast<const flat::PrimitiveType*>(&value);
            GenerateObjectMember("subtype", type->subtype);
            break;
        }
        case flat::Type::Kind::Identifier: {
            auto type = static_cast<const flat::IdentifierType*>(&value);
            GenerateObjectMember("identifier", type->name);
            GenerateObjectMember("nullable", type->nullability);
            break;
        }
        }
    });
}

void JSONGenerator::Generate(const raw::Constant& value) {
    GenerateObject([&]() {
        GenerateObjectMember("kind", ConstantKindName(value.kind), Position::First);

        switch (value.kind) {
        case raw::Constant::Kind::Identifier: {
            auto type = static_cast<const raw::IdentifierConstant*>(&value);
            GenerateObjectMember("identifier", type->identifier);
            break;
        }
        case raw::Constant::Kind::Literal: {
            auto type = static_cast<const raw::LiteralConstant*>(&value);
            GenerateObjectMember("literal", type->literal);
            break;
        }
        }
    });
}

void JSONGenerator::Generate(const raw::Attribute& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::First);
        if (value.value)
            GenerateObjectMember("value", value.value->location);
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
    std::vector<std::string> name_parts = {LongName(value)};
    Generate(name_parts);
}

void JSONGenerator::Generate(const flat::Const& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::First);
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        GenerateObjectMember("type", value.type);
        GenerateObjectMember("value", value.value);
    });
}

void JSONGenerator::Generate(const flat::Enum& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::First);
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        GenerateObjectMember("type", value.type);
        GenerateObjectMember("members", value.members);
    });
}

void JSONGenerator::Generate(const flat::Enum::Member& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::First);
        GenerateObjectMember("value", value.value);
    });
}

void JSONGenerator::Generate(const flat::Interface& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::First);
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        GenerateObjectMember("methods", value.methods);
    });
}

void JSONGenerator::Generate(const flat::Interface::Method& value) {
    GenerateObject([&]() {
        GenerateObjectMember("ordinal", value.ordinal, Position::First);
        GenerateObjectMember("name", value.name);
        GenerateObjectMember("has_request", value.maybe_request != nullptr);
        if (value.maybe_request != nullptr) {
            GenerateObjectMember("maybe_request", value.maybe_request->parameters);
            GenerateObjectMember("maybe_request_size", value.maybe_request->typeshape.Size());
            GenerateObjectMember("maybe_request_alignment", value.maybe_request->typeshape.Alignment());
        }
        GenerateObjectMember("has_response", value.maybe_response != nullptr);
        if (value.maybe_response != nullptr) {
            GenerateObjectMember("maybe_response", value.maybe_response->parameters);
            GenerateObjectMember("maybe_response_size", value.maybe_response->typeshape.Size());
            GenerateObjectMember("maybe_response_alignment", value.maybe_response->typeshape.Alignment());
        }
    });
}

void JSONGenerator::Generate(const flat::Interface::Method::Parameter& value) {
    GenerateObject([&]() {
        GenerateObjectMember("type", value.type, Position::First);
        GenerateObjectMember("name", value.name);
        GenerateObjectMember("size", value.fieldshape.Size());
        GenerateObjectMember("alignment", value.fieldshape.Alignment());
        GenerateObjectMember("offset", value.fieldshape.Offset());
    });
}

void JSONGenerator::Generate(const flat::Struct& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::First);
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        GenerateObjectMember("members", value.members);
        GenerateObjectMember("size", value.typeshape.Size());
        GenerateObjectMember("alignment", value.typeshape.Alignment());
    });
}

void JSONGenerator::Generate(const flat::Struct::Member& value) {
    GenerateObject([&]() {
        GenerateObjectMember("type", value.type, Position::First);
        GenerateObjectMember("name", value.name);
        if (value.maybe_default_value)
            GenerateObjectMember("maybe_default_value", value.maybe_default_value);
        GenerateObjectMember("size", value.fieldshape.Size());
        GenerateObjectMember("alignment", value.fieldshape.Alignment());
        GenerateObjectMember("offset", value.fieldshape.Offset());
    });
}

void JSONGenerator::Generate(const flat::Union& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::First);
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        GenerateObjectMember("members", value.members);
        GenerateObjectMember("size", value.typeshape.Size());
        GenerateObjectMember("alignment", value.typeshape.Alignment());
    });
}

void JSONGenerator::Generate(const flat::Union::Member& value) {
    GenerateObject([&]() {
        GenerateObjectMember("type", value.type, Position::First);
        GenerateObjectMember("name", value.name);
        GenerateObjectMember("size", value.fieldshape.Size());
        GenerateObjectMember("alignment", value.fieldshape.Alignment());
        GenerateObjectMember("offset", value.fieldshape.Offset());
    });
}

void JSONGenerator::GenerateDeclarationMapEntry(int count, const flat::Name& name, StringView decl) {
    if (count == 0)
        EmitNewlineAndIndent(&json_file_, ++indent_level_);
    else
        EmitObjectSeparator(&json_file_, indent_level_);
    EmitObjectKey(&json_file_, indent_level_, LongName(name));
    EmitString(&json_file_, decl);
}

std::ostringstream JSONGenerator::Produce() {
    indent_level_ = 0;
    GenerateObject([&]() {
        GenerateObjectMember("name", library_->library_name_, Position::First);
        // TODO(abarth): Produce library-dependencies data.
        GenerateObjectMember("library_dependencies", std::vector<bool>());
        GenerateObjectMember("const_declarations", library_->const_declarations_);
        GenerateObjectMember("enum_declarations", library_->enum_declarations_);
        GenerateObjectMember("interface_declarations", library_->interface_declarations_);
        GenerateObjectMember("struct_declarations", library_->struct_declarations_);
        GenerateObjectMember("union_declarations", library_->union_declarations_);
        GenerateObjectMember("declaration_order", library_->declaration_order_);

        EmitObjectSeparator(&json_file_, indent_level_);
        EmitObjectKey(&json_file_, indent_level_, "declarations");
        GenerateObject([&]() {
            int count = 0;
            for (const auto& decl : library_->const_declarations_)
                GenerateDeclarationMapEntry(count++, decl->name, "const");

            for (const auto& decl : library_->enum_declarations_)
                GenerateDeclarationMapEntry(count++, decl->name, "enum");

            for (const auto& decl : library_->interface_declarations_)
                GenerateDeclarationMapEntry(count++, decl->name, "interface");

            for (const auto& decl : library_->struct_declarations_)
                GenerateDeclarationMapEntry(count++, decl->name, "struct");

            for (const auto& decl : library_->union_declarations_)
                GenerateDeclarationMapEntry(count++, decl->name, "union");
        });
    });
    GenerateEOF();

    return std::move(json_file_);
}

} // namespace fidl
