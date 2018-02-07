 // Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/json_generator.h"

namespace fidl {

namespace {

constexpr const char* kIndent = "  ";

std::string LongName(const flat::Name& name) {
    // TODO(TO-701) Handle complex names.
    return name.get()->location.data();
}

std::string PrimitiveSubtypeName(ast::PrimitiveType::Subtype subtype) {
    switch (subtype) {
    case ast::PrimitiveType::Subtype::Int8:
        return "int8";
    case ast::PrimitiveType::Subtype::Int16:
        return "int16";
    case ast::PrimitiveType::Subtype::Int32:
        return "int32";
    case ast::PrimitiveType::Subtype::Int64:
        return "int64";
    case ast::PrimitiveType::Subtype::Uint8:
        return "uint8";
    case ast::PrimitiveType::Subtype::Uint16:
        return "uint16";
    case ast::PrimitiveType::Subtype::Uint32:
        return "uint32";
    case ast::PrimitiveType::Subtype::Uint64:
        return "uint64";
    case ast::PrimitiveType::Subtype::Bool:
        return "bool";
    case ast::PrimitiveType::Subtype::Status:
        return "status";
    case ast::PrimitiveType::Subtype::Float32:
        return "float32";
    case ast::PrimitiveType::Subtype::Float64:
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
    case types::HandleSubtype::Iomap:
        return "iomap";
    case types::HandleSubtype::Pci:
        return "pci";
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
    case types::HandleSubtype::Hypervisor:
        return "hypervisor";
    case types::HandleSubtype::Guest:
        return "guest";
    case types::HandleSubtype::Timer:
        return "timer";
    }
}

std::string LiteralKindName(ast::Literal::Kind kind) {
    switch (kind) {
    case ast::Literal::Kind::String:
        return "string";
    case ast::Literal::Kind::Numeric:
        return "numeric";
    case ast::Literal::Kind::True:
        return "true";
    case ast::Literal::Kind::False:
        return "false";
    case ast::Literal::Kind::Default:
        return "default";
    }
}

std::string TypeKindName(ast::Type::Kind kind) {
    switch (kind) {
    case ast::Type::Kind::Array:
        return "array";
    case ast::Type::Kind::Vector:
        return "vector";
    case ast::Type::Kind::String:
        return "string";
    case ast::Type::Kind::Handle:
        return "handle";
    case ast::Type::Kind::Request:
        return "request";
    case ast::Type::Kind::Primitive:
        return "primitive";
    case ast::Type::Kind::Identifier:
        return "identifier";
    }
}

std::string ConstantKindName(ast::Constant::Kind kind) {
    switch (kind) {
    case ast::Constant::Kind::Identifier:
        return "identifier";
    case ast::Constant::Kind::Literal:
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
        // TODO(abarth): Escape more characters.
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

template<typename Collection>
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

template<typename Callback>
void JSONGenerator::GenerateObject(Callback callback) {
    int original_indent_level = indent_level_;

    EmitObjectBegin(&json_file_);

    callback();

    if (indent_level_ > original_indent_level)
        EmitNewlineAndIndent(&json_file_, --indent_level_);

    EmitObjectEnd(&json_file_);
}

template<typename Type>
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

template<typename T>
void JSONGenerator::Generate(const std::unique_ptr<T>& value) {
    Generate(*value);
}

template<typename T>
void JSONGenerator::Generate(const std::vector<T>& value) {
    GenerateArray(value);
}

void JSONGenerator::Generate(bool value) {
    EmitBoolean(&json_file_, value);
}

void JSONGenerator::Generate(StringView value) {
    EmitString(&json_file_, value);
}

void JSONGenerator::Generate(types::HandleSubtype value) {
    EmitString(&json_file_, HandleSubtypeName(value));
}

void JSONGenerator::Generate(ast::Nullability value) {
    switch (value) {
    case ast::Nullability::Nullable:
        EmitBoolean(&json_file_, true);
        break;
    case ast::Nullability::Nonnullable:
        EmitBoolean(&json_file_, false);
        break;
    }
}

void JSONGenerator::Generate(ast::PrimitiveType::Subtype value) {
    EmitString(&json_file_, PrimitiveSubtypeName(value));
}

void JSONGenerator::Generate(const ast::Identifier& value) {
    EmitString(&json_file_, value.location.data());
}

void JSONGenerator::Generate(const ast::CompoundIdentifier& value) {
    Generate(value.components);
}

void JSONGenerator::Generate(const ast::Literal& value) {
    GenerateObject([&]() {
        GenerateObjectMember("kind", LiteralKindName(value.kind), Position::First);

        switch (value.kind) {
        case ast::Literal::Kind::String: {
            auto type = static_cast<const ast::StringLiteral*>(&value);
            EmitObjectSeparator(&json_file_, indent_level_);
            EmitObjectKey(&json_file_, indent_level_, "value");
            EmitLiteral(&json_file_, type->location.data());
            break;
        }
        case ast::Literal::Kind::Numeric: {
            auto type = static_cast<const ast::NumericLiteral*>(&value);
            GenerateObjectMember("value", type->location.data());
            break;
        }
        case ast::Literal::Kind::True: {
            break;
        }
        case ast::Literal::Kind::False: {
            break;
        }
        case ast::Literal::Kind::Default: {
            break;
        }
        }
    });
}

void JSONGenerator::Generate(const ast::Type& value) {
    GenerateObject([&]() {
        GenerateObjectMember("kind", TypeKindName(value.kind), Position::First);

        switch (value.kind) {
        case ast::Type::Kind::Array: {
            auto type = static_cast<const ast::ArrayType*>(&value);
            GenerateObjectMember("element_type", type->element_type);
            GenerateObjectMember("element_count", type->element_count);
            break;
        }
        case ast::Type::Kind::Vector: {
            auto type = static_cast<const ast::VectorType*>(&value);
            GenerateObjectMember("element_type", type->element_type);
            if (type->maybe_element_count)
                GenerateObjectMember("maybe_element_count", type->maybe_element_count);
            GenerateObjectMember("nullability", type->nullability);
            break;
        }
        case ast::Type::Kind::String: {
            auto type = static_cast<const ast::StringType*>(&value);
            if (type->maybe_element_count)
                GenerateObjectMember("maybe_element_count", type->maybe_element_count);
            GenerateObjectMember("nullability", type->nullability);
            break;
        }
        case ast::Type::Kind::Handle: {
            auto type = static_cast<const ast::HandleType*>(&value);
            GenerateObjectMember("subtype", type->subtype);
            GenerateObjectMember("nullability", type->nullability);
            break;
        }
        case ast::Type::Kind::Request: {
            auto type = static_cast<const ast::RequestType*>(&value);
            GenerateObjectMember("subtype", type->subtype);
            GenerateObjectMember("nullability", type->nullability);
            break;
        }
        case ast::Type::Kind::Primitive: {
            auto type = static_cast<const ast::PrimitiveType*>(&value);
            GenerateObjectMember("subtype", type->subtype);
            break;
        }
        case ast::Type::Kind::Identifier: {
            auto type = static_cast<const ast::IdentifierType*>(&value);
            GenerateObjectMember("identifier", type->identifier);
            GenerateObjectMember("nullability", type->nullability);
            break;
        }
        }
    });
}

void JSONGenerator::Generate(const ast::Constant& value) {
    GenerateObject([&]() {
        GenerateObjectMember("kind", ConstantKindName(value.kind), Position::First);

        switch (value.kind) {
        case ast::Constant::Kind::Identifier: {
            auto type = static_cast<const ast::IdentifierConstant*>(&value);
            GenerateObjectMember("identifier", type->identifier);
            break;
        }
        case ast::Constant::Kind::Literal: {
            auto type = static_cast<const ast::LiteralConstant*>(&value);
            GenerateObjectMember("literal", type->literal);
            break;
        }
        }
    });
}

void JSONGenerator::Generate(const flat::Ordinal& value) {
    EmitUint32(&json_file_, value.Value());
}

void JSONGenerator::Generate(const flat::Name& value) {
    EmitString(&json_file_, LongName(value));
}

void JSONGenerator::Generate(const flat::Const& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::First);
        GenerateObjectMember("type", value.type);
        GenerateObjectMember("value", value.value);
    });
}

void JSONGenerator::Generate(const flat::Enum& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::First);
        if (value.type->kind == ast::Type::Kind::Primitive) {
            auto type = static_cast<const ast::PrimitiveType*>(value.type.get());
            GenerateObjectMember("type", type->subtype);
        }
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
        GenerateObjectMember("methods", value.methods);
    });
}

void JSONGenerator::Generate(const flat::Interface::Method& value) {
    GenerateObject([&]() {
        GenerateObjectMember("ordinal", value.ordinal, Position::First);
        GenerateObjectMember("name", value.name);
        GenerateObjectMember("has_request", value.has_request);
        if (value.has_request)
            GenerateObjectMember("maybe_request", value.maybe_request);
        GenerateObjectMember("has_response", value.has_response);
        if (value.has_response)
            GenerateObjectMember("maybe_response", value.maybe_response);
    });
}

void JSONGenerator::Generate(const flat::Interface::Method::Parameter& value) {
    GenerateObject([&]() {
        GenerateObjectMember("type", value.type, Position::First);
        GenerateObjectMember("name", value.name);
    });
}

void JSONGenerator::Generate(const flat::Struct& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::First);
        GenerateObjectMember("members", value.members);
    });
}

void JSONGenerator::Generate(const flat::Struct::Member& value) {
    GenerateObject([&]() {
        GenerateObjectMember("type", value.type, Position::First);
        GenerateObjectMember("name", value.name);
        if (value.default_value)
            GenerateObjectMember("maybe_default_value", value.default_value);
    });
}

void JSONGenerator::Generate(const flat::Union& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::First);
        GenerateObjectMember("members", value.members);
    });
}

void JSONGenerator::Generate(const flat::Union::Member& value) {
    GenerateObject([&]() {
        GenerateObjectMember("type", value.type, Position::First);
        GenerateObjectMember("name", value.name);
    });
}

void JSONGenerator::ProduceJSON(std::ostringstream* json_file_out) {
    indent_level_ = 0;
    GenerateObject([&]() {
        // TODO(abarth): Produce library-dependencies data.
        GenerateObjectMember("library_dependencies", std::vector<bool>(), Position::First);
        GenerateObjectMember("const_declarations", library_->const_declarations_);
        GenerateObjectMember("enum_declarations", library_->enum_declarations_);
        GenerateObjectMember("interface_declarations", library_->interface_declarations_);
        GenerateObjectMember("struct_declarations", library_->struct_declarations_);
        GenerateObjectMember("union_declarations", library_->union_declarations_);
    });

    *json_file_out = std::move(json_file_);
}

} // namespace fidl
