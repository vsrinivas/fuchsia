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

// ConstantStyle indicates whether the constant value to be emitted should be
// directly placed in the JSON output, or whether is must be wrapped in a
// string.
enum ConstantStyle {
    kAsConstant,
    kAsString,
};

void EmitBoolean(std::ostream* file, bool value, ConstantStyle style = kAsConstant) {
    if (style == kAsString)
        *file << "\"";
    if (value)
        *file << "true";
    else
        *file << "false";
    if (style == kAsString)
        *file << "\"";
}

void EmitString(std::ostream* file, std::string_view value) {
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
        case '\n':
            *file << "\\n";
            break;
        // TODO(FIDL-28): Escape more characters.
        default:
            *file << c;
            break;
        }
    }
    *file << "\"";
}

void EmitLiteral(std::ostream* file, std::string_view value) {
    file->rdbuf()->sputn(value.data(), value.size());
}

template <typename ValueType>
void EmitNumeric(std::ostream* file, ValueType value, ConstantStyle style = kAsConstant) {
    static_assert(std::is_arithmetic<ValueType>::value && !std::is_same<ValueType, bool>::value,
                  "EmitNumeric can only be used with a numeric ValueType!");
    static_assert(std::is_arithmetic<ValueType>::value && !std::is_same<ValueType, uint8_t>::value,
                  "EmitNumeric does not work for uint8_t, upcast to uint64_t");
    static_assert(std::is_arithmetic<ValueType>::value && !std::is_same<ValueType, int8_t>::value,
                  "EmitNumeric does not work for int8_t, upcast to int64_t");

    switch (style) {
    case ConstantStyle::kAsConstant:
        *file << value;
        break;
    case ConstantStyle::kAsString:
        *file << "\"" << value << "\"";
        break;
    }
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

void EmitObjectKey(std::ostream* file, int indent_level, std::string_view key) {
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

// Temporarily specializing for structs to avoid printing anonymous
// declarations.
template <>
void JSONGenerator::GenerateArray(
    std::vector<std::unique_ptr<flat::Struct>>::const_iterator begin,
    std::vector<std::unique_ptr<flat::Struct>>::const_iterator end) {
    EmitArrayBegin(&json_file_);

    bool is_first = true;
    for (std::vector<std::unique_ptr<flat::Struct>>::const_iterator it = begin; it != end; ++it) {
        if ((*it)->anonymous)
            continue;
        if (is_first) {
            EmitNewlineAndIndent(&json_file_, ++indent_level_);
            is_first = false;
        } else {
            EmitArraySeparator(&json_file_, indent_level_);
        }
        Generate(*it);
    }
    if (!is_first)
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
void JSONGenerator::GenerateObjectMember(std::string_view key, const Type& value, Position position) {
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

void JSONGenerator::Generate(std::string_view value) {
    EmitString(&json_file_, value);
}

void JSONGenerator::Generate(SourceLocation value) {
    EmitString(&json_file_, value.data());
}

void JSONGenerator::Generate(NameLocation value) {
    GenerateObject([&]() {
        GenerateObjectMember("filename", value.filename, Position::kFirst);
        GenerateObjectMember("line", (uint32_t)value.position.line);
        GenerateObjectMember("column", (uint32_t)value.position.column);
    });
}

void JSONGenerator::Generate(uint32_t value) {
    EmitNumeric(&json_file_, value);
}

void JSONGenerator::Generate(const flat::ConstantValue& value) {
    switch (value.kind) {
    case flat::ConstantValue::Kind::kUint8: {
        auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<uint8_t>&>(value);
        EmitNumeric(&json_file_, static_cast<uint64_t>(numeric_constant), kAsString);
        break;
    }
    case flat::ConstantValue::Kind::kUint16: {
        auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<uint16_t>&>(value);
        EmitNumeric(&json_file_, static_cast<uint16_t>(numeric_constant), kAsString);
        break;
    }
    case flat::ConstantValue::Kind::kUint32: {
        auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<uint32_t>&>(value);
        EmitNumeric(&json_file_, static_cast<uint32_t>(numeric_constant), kAsString);
        break;
    }
    case flat::ConstantValue::Kind::kUint64: {
        auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<uint64_t>&>(value);
        EmitNumeric(&json_file_, static_cast<uint64_t>(numeric_constant), kAsString);
        break;
    }
    case flat::ConstantValue::Kind::kInt8: {
        auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<int8_t>&>(value);
        EmitNumeric(&json_file_, static_cast<int64_t>(numeric_constant), kAsString);
        break;
    }
    case flat::ConstantValue::Kind::kInt16: {
        auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<int16_t>&>(value);
        EmitNumeric(&json_file_, static_cast<int16_t>(numeric_constant), kAsString);
        break;
    }
    case flat::ConstantValue::Kind::kInt32: {
        auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<int32_t>&>(value);
        EmitNumeric(&json_file_, static_cast<int32_t>(numeric_constant), kAsString);
        break;
    }
    case flat::ConstantValue::Kind::kInt64: {
        auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<int64_t>&>(value);
        EmitNumeric(&json_file_, static_cast<int64_t>(numeric_constant), kAsString);
        break;
    }
    case flat::ConstantValue::Kind::kFloat32: {
        auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<float>&>(value);
        EmitNumeric(&json_file_, static_cast<float>(numeric_constant), kAsString);
        break;
    }
    case flat::ConstantValue::Kind::kFloat64: {
        auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<double>&>(value);
        EmitNumeric(&json_file_, static_cast<double>(numeric_constant), kAsString);
        break;
    }
    case flat::ConstantValue::Kind::kBool: {
        auto bool_constant = reinterpret_cast<const flat::BoolConstantValue&>(value);
        EmitBoolean(&json_file_, static_cast<bool>(bool_constant), kAsString);
        break;
    }
    case flat::ConstantValue::Kind::kString: {
        auto string_constant = reinterpret_cast<const flat::StringConstantValue&>(value);
        EmitLiteral(&json_file_, string_constant.value);
        break;
    }
    } // switch
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

void JSONGenerator::Generate(const flat::LiteralConstant& value) {
    GenerateObject([&]() {
        GenerateObjectMember("kind", NameRawLiteralKind(value.literal->kind), Position::kFirst);

        // TODO(FIDL-486): Since some constants are not properly resolved during
        // library compilation, we must be careful in emitting the resolved
        // value. Currently, we fall back using the original value, despite this
        // being problematic in the case of binary literals.
        if (value.IsResolved()) {
            GenerateObjectMember("value", value.Value());
        } else {
            switch (value.literal->kind) {
            case raw::Literal::Kind::kString: {
                auto string_literal = static_cast<const raw::StringLiteral*>(value.literal.get());
                EmitObjectSeparator(&json_file_, indent_level_);
                EmitObjectKey(&json_file_, indent_level_, "value");
                EmitLiteral(&json_file_, string_literal->location().data());
                break;
            }
            case raw::Literal::Kind::kNumeric:
            case raw::Literal::Kind::kTrue:
            case raw::Literal::Kind::kFalse:
                GenerateObjectMember("value", value.literal->location().data());
                break;
            } // switch
        }
        GenerateObjectMember("expression", value.literal->location().data());
    });
}

void JSONGenerator::Generate(const flat::Constant& value) {
    GenerateObject([&]() {
        switch (value.kind) {
        case flat::Constant::Kind::kIdentifier: {
            GenerateObjectMember("kind", NameFlatConstantKind(value.kind), Position::kFirst);
            auto type = static_cast<const flat::IdentifierConstant*>(&value);
            GenerateObjectMember("identifier", type->name);
            break;
        }
        case flat::Constant::Kind::kLiteral: {
            GenerateObjectMember("kind", NameFlatConstantKind(value.kind), Position::kFirst);
            auto& type = static_cast<const flat::LiteralConstant&>(value);
            GenerateObjectMember("literal", type);
            break;
        }
        case flat::Constant::Kind::kSynthesized: {
            // TODO(pascallouis): We should explore exposing these in the JSON IR, such that the
            // implicit bounds are made explicit by fidlc, rather than sprinkled throughout all
            // backends.
            //
            // For now, do not emit synthesized constants
            break;
        }
        }
    });
}

void JSONGenerator::Generate(const flat::Type* value) {
    GenerateObject([&]() {
        GenerateObjectMember("kind", NameFlatTypeKind(value->kind), Position::kFirst);

        switch (value->kind) {
        case flat::Type::Kind::kArray: {
            auto type = static_cast<const flat::ArrayType*>(value);
            GenerateObjectMember("element_type", type->element_type);
            GenerateObjectMember("element_count", type->element_count->value);
            break;
        }
        case flat::Type::Kind::kVector: {
            auto type = static_cast<const flat::VectorType*>(value);
            GenerateObjectMember("element_type", type->element_type);
            if (*type->element_count < flat::Size::Max())
                GenerateObjectMember("maybe_element_count", type->element_count->value);
            GenerateObjectMember("nullable", type->nullability);
            break;
        }
        case flat::Type::Kind::kString: {
            auto type = static_cast<const flat::StringType*>(value);
            if (*type->max_size < flat::Size::Max())
                GenerateObjectMember("maybe_element_count", type->max_size->value);
            GenerateObjectMember("nullable", type->nullability);
            break;
        }
        case flat::Type::Kind::kHandle: {
            auto type = static_cast<const flat::HandleType*>(value);
            GenerateObjectMember("subtype", type->subtype);
            GenerateObjectMember("nullable", type->nullability);
            break;
        }
        case flat::Type::Kind::kRequestHandle: {
            auto type = static_cast<const flat::RequestHandleType*>(value);
            GenerateObjectMember("subtype", type->interface_type->name);
            GenerateObjectMember("nullable", type->nullability);
            break;
        }
        case flat::Type::Kind::kPrimitive: {
            auto type = static_cast<const flat::PrimitiveType*>(value);
            GenerateObjectMember("subtype", type->subtype);
            break;
        }
        case flat::Type::Kind::kIdentifier: {
            auto type = static_cast<const flat::IdentifierType*>(value);
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
        if (value.value != "")
            GenerateObjectMember("value", value.value);
        else
            GenerateObjectMember("value", std::string_view());
    });
}

void JSONGenerator::Generate(const raw::AttributeList& value) {
    Generate(value.attributes);
}

void JSONGenerator::Generate(const raw::Ordinal& value) {
    EmitNumeric(&json_file_, value.value);
}

void JSONGenerator::Generate(const flat::Name& value) {
    // These look like (when there is a library)
    //     { "LIB.LIB.LIB", "ID" }
    // or (when there is not)
    //     { "ID" }
    Generate(NameName(value, ".", "/"));
}

void JSONGenerator::Generate(const flat::Bits& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::kFirst);
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        GenerateObjectMember("type", value.subtype_ctor->type);
        // TODO(FIDL-324): When all numbers are wrapped as string, we can simply
        // call GenerateObjectMember directly.
        GenerateObjectPunctuation(Position::kSubsequent);
        EmitObjectKey(&json_file_, indent_level_, "mask");
        EmitNumeric(&json_file_, value.mask, kAsString);
        GenerateObjectMember("members", value.members);
    });
}

void JSONGenerator::Generate(const flat::Bits::Member& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::kFirst);
        GenerateObjectMember("value", value.value);
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
    });
}

void JSONGenerator::Generate(const flat::Const& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::kFirst);
        GenerateObjectMember("location", NameLocation(value.name));
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        GenerateObjectMember("type", value.type_ctor->type);
        GenerateObjectMember("value", value.value);
    });
}

void JSONGenerator::Generate(const flat::Enum& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::kFirst);
        GenerateObjectMember("location", NameLocation(value.name));
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        GenerateObjectMember("type", value.type->subtype);
        GenerateObjectMember("members", value.members);
    });
}

void JSONGenerator::Generate(const flat::Enum::Member& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::kFirst);
        GenerateObjectMember("location", NameLocation(value.name));
        GenerateObjectMember("value", value.value);
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
    });
}

void JSONGenerator::Generate(const flat::Interface& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::kFirst);
        GenerateObjectMember("location", NameLocation(value.name));
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
        GenerateObjectMember("generated_ordinal", value.generated_ordinal);
        GenerateObjectMember("name", value.name);
        GenerateObjectMember("location", NameLocation(value.name));
        GenerateObjectMember("has_request", value.maybe_request != nullptr);
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        if (value.maybe_request != nullptr) {
            GenerateRequest("maybe_request", *value.maybe_request);
        }
        GenerateObjectMember("has_response", value.maybe_response != nullptr);
        if (value.maybe_response != nullptr) {
            GenerateRequest("maybe_response", *value.maybe_response);
        }
    });
}

void JSONGenerator::GenerateRequest(const std::string& prefix, const flat::Struct& value) {
    GenerateObjectMember(prefix, value.members);
    GenerateObjectMember(prefix + "_size", value.typeshape.Size());
    GenerateObjectMember(prefix + "_alignment", value.typeshape.Alignment());
    GenerateObjectMember(prefix + "_has_padding", value.typeshape.HasPadding());
}

void JSONGenerator::Generate(const flat::Struct& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::kFirst);
        GenerateObjectMember("location", NameLocation(value.name));
        GenerateObjectMember("anonymous", value.anonymous);
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        GenerateObjectMember("members", value.members);
        GenerateObjectMember("size", value.typeshape.Size());
        GenerateObjectMember("max_out_of_line", value.typeshape.MaxOutOfLine());
        GenerateObjectMember("alignment", value.typeshape.Alignment());
        GenerateObjectMember("max_handles", value.typeshape.MaxHandles());
        GenerateObjectMember("has_padding", value.typeshape.HasPadding());
    });
}

void JSONGenerator::Generate(const flat::Struct::Member& value) {
    GenerateObject([&]() {
        GenerateObjectMember("type", value.type_ctor->type, Position::kFirst);
        GenerateObjectMember("name", value.name);
        GenerateObjectMember("location", NameLocation(value.name));
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        if (value.maybe_default_value)
            GenerateObjectMember("maybe_default_value", value.maybe_default_value);
        GenerateObjectMember("size", value.fieldshape.Size());
        GenerateObjectMember("max_out_of_line", value.fieldshape.MaxOutOfLine());
        GenerateObjectMember("alignment", value.fieldshape.Alignment());
        GenerateObjectMember("offset", value.fieldshape.Offset());
        GenerateObjectMember("max_handles", value.fieldshape.MaxHandles());
    });
}

void JSONGenerator::Generate(const flat::Table& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::kFirst);
        GenerateObjectMember("location", NameLocation(value.name));
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        GenerateObjectMember("members", value.members);
        GenerateObjectMember("size", value.typeshape.Size());
        GenerateObjectMember("max_out_of_line", value.typeshape.MaxOutOfLine());
        GenerateObjectMember("alignment", value.typeshape.Alignment());
        GenerateObjectMember("max_handles", value.typeshape.MaxHandles());
    });
}

void JSONGenerator::Generate(const flat::Table::Member& value) {
    GenerateObject([&]() {
        GenerateObjectMember("ordinal", *value.ordinal, Position::kFirst);
        if (value.maybe_used) {
            assert(!value.maybe_location);
            GenerateObjectMember("reserved", false);
            GenerateObjectMember("type", value.maybe_used->type_ctor->type);
            GenerateObjectMember("name", value.maybe_used->name);
            GenerateObjectMember("location", NameLocation(value.maybe_used->name));
            if (value.maybe_used->attributes)
                GenerateObjectMember("maybe_attributes", value.maybe_used->attributes);
            // TODO(FIDL-609): Support defaults on tables.
            GenerateObjectMember("size", value.maybe_used->typeshape.Size());
            GenerateObjectMember("max_out_of_line", value.maybe_used->typeshape.MaxOutOfLine());
            GenerateObjectMember("alignment", value.maybe_used->typeshape.Alignment());
            GenerateObjectMember("max_handles", value.maybe_used->typeshape.MaxHandles());
        } else {
            assert(value.maybe_location);
            GenerateObjectMember("reserved", true);
            GenerateObjectMember("location", NameLocation(*value.maybe_location));
        }
    });
}

void JSONGenerator::Generate(const flat::Union& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::kFirst);
        GenerateObjectMember("location", NameLocation(value.name));
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        GenerateObjectMember("members", value.members);
        GenerateObjectMember("size", value.typeshape.Size());
        GenerateObjectMember("max_out_of_line", value.typeshape.MaxOutOfLine());
        GenerateObjectMember("alignment", value.typeshape.Alignment());
        GenerateObjectMember("max_handles", value.typeshape.MaxHandles());
    });
}

void JSONGenerator::Generate(const flat::Union::Member& value) {
    GenerateObject([&]() {
        GenerateObjectMember("type", value.type_ctor->type, Position::kFirst);
        GenerateObjectMember("name", value.name);
        GenerateObjectMember("location", NameLocation(value.name));
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        GenerateObjectMember("size", value.fieldshape.Size());
        GenerateObjectMember("max_out_of_line", value.fieldshape.MaxOutOfLine());
        GenerateObjectMember("alignment", value.fieldshape.Alignment());
        GenerateObjectMember("offset", value.fieldshape.Offset());
    });
}

void JSONGenerator::Generate(const flat::XUnion& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::kFirst);
        GenerateObjectMember("location", NameLocation(value.name));
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        GenerateObjectMember("members", value.members);
        GenerateObjectMember("size", value.typeshape.Size());
        GenerateObjectMember("max_out_of_line", value.typeshape.MaxOutOfLine());
        GenerateObjectMember("alignment", value.typeshape.Alignment());
        GenerateObjectMember("max_handles", value.typeshape.MaxHandles());
    });
}

void JSONGenerator::Generate(const flat::XUnion::Member& value) {
    GenerateObject([&]() {
        GenerateObjectMember("ordinal", value.ordinal, Position::kFirst);
        GenerateObjectMember("type", value.type_ctor->type);
        GenerateObjectMember("name", value.name);
        GenerateObjectMember("location", NameLocation(value.name));
        if (value.attributes)
            GenerateObjectMember("maybe_attributes", value.attributes);
        GenerateObjectMember("size", value.fieldshape.Size());
        GenerateObjectMember("max_out_of_line", value.fieldshape.MaxOutOfLine());
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

void JSONGenerator::GenerateDeclarationsEntry(int count, const flat::Name& name, std::string_view decl) {
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
        for (const auto& decl : library->bits_declarations_)
            GenerateDeclarationsEntry(count++, decl->name, "bits");

        for (const auto& decl : library->const_declarations_)
            GenerateDeclarationsEntry(count++, decl->name, "const");

        for (const auto& decl : library->enum_declarations_)
            GenerateDeclarationsEntry(count++, decl->name, "enum");

        for (const auto& decl : library->interface_declarations_)
            GenerateDeclarationsEntry(count++, decl->name, "interface");

        for (const auto& decl : library->struct_declarations_) {
            if (decl->anonymous)
                continue;
            GenerateDeclarationsEntry(count++, decl->name, "struct");
        }

        for (const auto& decl : library->table_declarations_)
            GenerateDeclarationsEntry(count++, decl->name, "table");

        for (const auto& decl : library->union_declarations_)
            GenerateDeclarationsEntry(count++, decl->name, "union");

        for (const auto& decl : library->xunion_declarations_)
            GenerateDeclarationsEntry(count++, decl->name, "xunion");
    });
}

namespace {

struct LibraryComparator {
    bool operator()(const flat::Library* lhs, const flat::Library* rhs) const {
        assert(!lhs->name().empty());
        assert(!rhs->name().empty());
        return lhs->name() < rhs->name();
    }
};

std::set<const flat::Library*, LibraryComparator>
TransitiveDependencies(const flat::Library* library) {
    std::set<const flat::Library*, LibraryComparator> dependencies;
    for (const auto& dep_library : library->dependencies()) {
        if (!dep_library->HasAttribute("Internal")) {
            dependencies.insert(dep_library);
        }
    }
    // Discover additional dependencies that are required to support
    // cross-library protocol composition.
    for (const auto& interface : library->interface_declarations_) {
        for (const auto method : interface->all_methods) {
            dependencies.insert(method->owning_interface->name.library());
        }
    }
    dependencies.erase(library);
    return dependencies;
}

} // namespace

std::ostringstream JSONGenerator::Produce() {
    indent_level_ = 0;
    GenerateObject([&]() {
        GenerateObjectMember("version", std::string_view("0.0.1"), Position::kFirst);

        GenerateObjectMember("name", LibraryName(library_, "."));

        GenerateObjectPunctuation(Position::kSubsequent);
        EmitObjectKey(&json_file_, indent_level_, "library_dependencies");
        GenerateArray(TransitiveDependencies(library_));

        GenerateObjectMember("bits_declarations", library_->bits_declarations_);
        GenerateObjectMember("const_declarations", library_->const_declarations_);
        GenerateObjectMember("enum_declarations", library_->enum_declarations_);
        GenerateObjectMember("interface_declarations", library_->interface_declarations_);
        GenerateObjectMember("struct_declarations", library_->struct_declarations_);
        GenerateObjectMember("table_declarations", library_->table_declarations_);
        GenerateObjectMember("union_declarations", library_->union_declarations_);
        GenerateObjectMember("xunion_declarations", library_->xunion_declarations_);

        // The library's declaration_order_ contains all the declarations for all
        // transitive dependencies. The backend only needs the declaration order
        // for this specific library.
        std::vector<std::string> declaration_order;
        for (flat::Decl* decl : library_->declaration_order_) {
            if (decl->kind == flat::Decl::Kind::kStruct) {
                auto struct_decl = static_cast<flat::Struct*>(decl);
                if (struct_decl->anonymous)
                    continue;
            }
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
