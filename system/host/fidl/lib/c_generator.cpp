 // Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/c_generator.h"

namespace fidl {

namespace {

// Various string values are looked up or computed in these
// functions. Nothing else should be dealing in string literals, or
// computing strings from these or AST values.

constexpr const char* kIndent = "    ";

std::string ShortName(const std::unique_ptr<ast::Identifier>& name) {
    // TODO(TO-704) C name escaping and ergonomics.
    return name->location.data();
}

std::string LongName(const flat::Name& name) {
    // TODO(TO-701) Handle complex names.
    return name.get()->location.data();
}

std::string UnionTagName(StringView union_name,
                         const std::unique_ptr<ast::Identifier>& member_name) {
    return std::string(union_name) + "_tag_" + ShortName(member_name);
}

std::string PrimitiveTypeName(const ast::PrimitiveType* type) {
    switch (type->subtype) {
    case ast::PrimitiveType::Subtype::Int8:
        return "int8_t";
    case ast::PrimitiveType::Subtype::Int16:
        return "int16_t";
    case ast::PrimitiveType::Subtype::Int32:
        return "int32_t";
    case ast::PrimitiveType::Subtype::Int64:
        return "int64_t";
    case ast::PrimitiveType::Subtype::Uint8:
        return "uint8_t";
    case ast::PrimitiveType::Subtype::Uint16:
        return "uint16_t";
    case ast::PrimitiveType::Subtype::Uint32:
        return "uint32_t";
    case ast::PrimitiveType::Subtype::Uint64:
        return "uint64_t";
    case ast::PrimitiveType::Subtype::Bool:
        return "bool";
    case ast::PrimitiveType::Subtype::Status:
        return "zx_status_t";
    case ast::PrimitiveType::Subtype::Float32:
        return "float";
    case ast::PrimitiveType::Subtype::Float64:
        return "double";
    }
}

std::string IntegerCTypedefName(CGenerator::IntegerConstantType type) {
    switch (type) {
    case CGenerator::IntegerConstantType::kStatus:
        return "zx_status_t";
    case CGenerator::IntegerConstantType::kInt8:
        return "int8_t";
    case CGenerator::IntegerConstantType::kInt16:
        return "int16_t";
    case CGenerator::IntegerConstantType::kInt32:
        return "int32_t";
    case CGenerator::IntegerConstantType::kInt64:
        return "int64_t";
    case CGenerator::IntegerConstantType::kUint8:
        return "uint8_t";
    case CGenerator::IntegerConstantType::kUint16:
        return "uint16_t";
    case CGenerator::IntegerConstantType::kUint32:
        return "uint32_t";
    case CGenerator::IntegerConstantType::kUint64:
        return "uint64_t";
    }
}

std::string IntegerCConstantMacro(CGenerator::IntegerConstantType type) {
    switch (type) {
    case CGenerator::IntegerConstantType::kInt8:
        return "INT8_C";
    case CGenerator::IntegerConstantType::kInt16:
        return "INT16_C";
    case CGenerator::IntegerConstantType::kInt32:
    case CGenerator::IntegerConstantType::kStatus:
        return "INT32_C";
    case CGenerator::IntegerConstantType::kInt64:
        return "INT64_C";
    case CGenerator::IntegerConstantType::kUint8:
        return "UINT8_C";
    case CGenerator::IntegerConstantType::kUint16:
        return "UINT16_C";
    case CGenerator::IntegerConstantType::kUint32:
        return "UINT32_C";
    case CGenerator::IntegerConstantType::kUint64:
        return "UINT64_C";
    }
}

std::string TypeName(const ast::Type* type) {
    for (;;) {
        switch (type->kind) {
        case ast::Type::Kind::Handle:
        case ast::Type::Kind::Request:
            return "zx_handle_t";

        case ast::Type::Kind::Vector:
            return "fidl_vector_t";
        case ast::Type::Kind::String:
            return "fidl_string_t";

        case ast::Type::Kind::Primitive: {
            auto primitive_type = static_cast<const ast::PrimitiveType*>(type);
            return PrimitiveTypeName(primitive_type);
        }

        case ast::Type::Kind::Array: {
            auto array_type = static_cast<const ast::ArrayType*>(type);
            type = array_type->element_type.get();
            continue;
        }

        case ast::Type::Kind::Identifier: {
            auto identifier_type = static_cast<const ast::IdentifierType*>(type);
            // TODO(TO-701) Handle longer names.
            const auto& components = identifier_type->identifier->components;
            assert(components.size() == 1);
            std::string name = components[0]->location.data();
            if (identifier_type->nullability == ast::Nullability::Nullable) {
                name.push_back('*');
            }
            return name;
        }
        }
    }
}

CGenerator::Member MessageHeader() {
    return {"fidl_message_header_t", "hdr", {}};
}

// Functions named "Emit..." are called to actually emit to an std::ostream
// is here. No other functions should directly emit to the streams.

std::ostream& operator<<(std::ostream& stream, StringView view) {
    stream.rdbuf()->sputn(view.data(), view.size());
    return stream;
}

void EmitHeaderGuard(std::ostream* file) {
    // TODO(704) Generate an appropriate header guard name.
    *file << "#pragma once\n";
}

void EmitIncludeHeader(std::ostream* file, StringView header) {
    *file << "#include " << header << "\n";
}

void EmitBeginExternC(std::ostream* file) {
    *file << "#if defined(__cplusplus)\nextern \"C\" {\n#endif\n";
}

void EmitEndExternC(std::ostream* file) {
    *file << "#if defined(__cplusplus)\n}\n#endif\n";
}

void EmitBlank(std::ostream* file) {
    *file << "\n";
}

// Various computational helper routines.

CGenerator::IntegerConstantType EnumType(ast::PrimitiveType::Subtype type) {
    switch (type) {
    case ast::PrimitiveType::Subtype::Int8:
        return CGenerator::IntegerConstantType::kInt8;
    case ast::PrimitiveType::Subtype::Int16:
        return CGenerator::IntegerConstantType::kInt16;
    case ast::PrimitiveType::Subtype::Int32:
        return CGenerator::IntegerConstantType::kInt32;
    case ast::PrimitiveType::Subtype::Int64:
        return CGenerator::IntegerConstantType::kInt64;
    case ast::PrimitiveType::Subtype::Uint8:
        return CGenerator::IntegerConstantType::kUint8;
    case ast::PrimitiveType::Subtype::Uint16:
        return CGenerator::IntegerConstantType::kUint16;
    case ast::PrimitiveType::Subtype::Uint32:
        return CGenerator::IntegerConstantType::kUint32;
    case ast::PrimitiveType::Subtype::Uint64:
        return CGenerator::IntegerConstantType::kUint64;
    case ast::PrimitiveType::Subtype::Bool:
    case ast::PrimitiveType::Subtype::Status:
    case ast::PrimitiveType::Subtype::Float32:
    case ast::PrimitiveType::Subtype::Float64:
    default:
        assert(false && "bad primitive type for an enum");
        break;
    }
}

void EnumValue(ast::PrimitiveType::Subtype type, const ast::Constant* constant,
               Library* library, std::string* out_value) {
    // TODO(kulakowski) Move this into library resolution.

    std::ostringstream member_value;

    switch (type) {
    case ast::PrimitiveType::Subtype::Int8: {
        int8_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        // The char-sized overloads of operator<< here print
        // the character value, not the numeric value, so cast up.
        member_value << static_cast<int>(value);
        break;
    }
    case ast::PrimitiveType::Subtype::Int16: {
        int16_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        member_value << value;
        break;
    }
    case ast::PrimitiveType::Subtype::Int32: {
        int32_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        member_value << value;
        break;
    }
    case ast::PrimitiveType::Subtype::Int64: {
        int64_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        member_value << value;
        break;
    }
    case ast::PrimitiveType::Subtype::Uint8: {
        uint8_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        // The char-sized overloads of operator<< here print
        // the character value, not the numeric value, so cast up.
        member_value << static_cast<unsigned int>(value);
        break;
    }
    case ast::PrimitiveType::Subtype::Uint16: {
        uint16_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        member_value << value;
        break;
    }
    case ast::PrimitiveType::Subtype::Uint32: {
        uint32_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        member_value << value;
        break;
    }
    case ast::PrimitiveType::Subtype::Uint64: {
        uint64_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        member_value << value;
        break;
    }
    case ast::PrimitiveType::Subtype::Bool:
    case ast::PrimitiveType::Subtype::Status:
    case ast::PrimitiveType::Subtype::Float32:
    case ast::PrimitiveType::Subtype::Float64:
        assert(false && "bad primitive type for an enum");
        break;
    }

    *out_value = member_value.str();
}

std::vector<uint32_t> ArrayCounts(Library* library, const ast::Type* type) {
    std::vector<uint32_t> array_counts;
    for (;;) {
        switch (type->kind) {
        default: { return array_counts; }
        case ast::Type::Kind::Array: {
            auto array_type = static_cast<const ast::ArrayType*>(type);
            const ast::Constant* count_constant = array_type->element_count.get();
            uint32_t array_count;
            bool success = library->ParseIntegerConstant(count_constant, &array_count);
            // TODO(TO-702) Better error handling around failure to validate constants.
            if (!success) {
                // __builtin_trap();
            }
            array_counts.push_back(array_count);
            type = array_type->element_type.get();
            continue;
        }
        }
    }
}

CGenerator::Member CreateMember(Library* library, const ast::Type* type, StringView name) {
    auto type_name = TypeName(type);
    std::vector<uint32_t> array_counts = ArrayCounts(library, type);
    return CGenerator::Member{type_name, name, std::move(array_counts)};
}

std::vector<CGenerator::Member>
GenerateMembers(Library* library, const std::vector<flat::Union::Member>& union_members) {
    std::vector<CGenerator::Member> members;
    members.reserve(union_members.size());
    for (const auto& union_member : union_members) {
        const ast::Type* union_member_type = union_member.type.get();
        auto union_member_name = ShortName(union_member.name);
        members.push_back(CreateMember(library, union_member_type, union_member_name));
    }
    return members;
}

} // namespace

void CGenerator::GeneratePrologues() {
    EmitHeaderGuard(&header_file_);
    EmitBlank(&header_file_);
    EmitIncludeHeader(&header_file_, "<stdbool.h>");
    EmitIncludeHeader(&header_file_, "<stdint.h>");
    EmitIncludeHeader(&header_file_, "<fidl/coding.h>");
    EmitIncludeHeader(&header_file_, "<zircon/fidl.h>");
    EmitIncludeHeader(&header_file_, "<zircon/syscalls/object.h>");
    EmitIncludeHeader(&header_file_, "<zircon/types.h>");
    EmitBlank(&header_file_);
    EmitBeginExternC(&header_file_);
    EmitBlank(&header_file_);
}

void CGenerator::GenerateEpilogues() {
    EmitEndExternC(&header_file_);
}

void CGenerator::GenerateIntegerDefine(StringView name, IntegerConstantType type,
                                       StringView value) {
    std::string literal_macro = IntegerCConstantMacro(type);
    header_file_ << "#define " << name << " " << literal_macro << "(" << value << ")\n";
}

void CGenerator::GenerateIntegerTypedef(IntegerConstantType type, StringView name) {
    std::string underlying_type = IntegerCTypedefName(type);
    header_file_ << "typedef " << underlying_type << " " << name << ";\n";
}

void CGenerator::GenerateStructTypedef(StringView name) {
    header_file_ << "typedef struct " << name << " " << name << ";\n";
}

void CGenerator::GenerateStructDeclaration(StringView name, const std::vector<Member>& members) {
    header_file_ << "struct " << name << " {\n";
    for (const auto& member : members) {
        header_file_ << kIndent << member.type << " " << member.name;
        for (uint32_t array_count : member.array_counts) {
            header_file_ << "[" << array_count << "]";
        }
        header_file_ << ";\n";
    }
    header_file_ << "};\n";
}

void CGenerator::GenerateTaggedUnionDeclaration(StringView name,
                                                const std::vector<Member>& members) {
    header_file_ << "struct " << name << " {\n";
    header_file_ << kIndent << "fidl_union_tag_t tag;\n";
    header_file_ << kIndent << "union {\n";
    for (const auto& member : members) {
        header_file_ << kIndent << kIndent << member.type << " " << member.name;
        for (uint32_t array_count : member.array_counts) {
            header_file_ << "[" << array_count << "]";
        }
        header_file_ << ";\n";
    }
    header_file_ << kIndent << "};\n";
    header_file_ << "};\n";
}

// TODO(TO-702) These should maybe check for global name
// collisions? Otherwise, is there some other way they should fail?
std::vector<CGenerator::NamedConst> CGenerator::NameConsts(const std::vector<flat::Const>& const_infos) {
    std::vector<CGenerator::NamedConst> named_consts;
    for (const auto& const_info : const_infos) {
        named_consts.push_back({"", const_info});
    }
    return named_consts;
}

std::vector<CGenerator::NamedEnum> CGenerator::NameEnums(const std::vector<flat::Enum>& enum_infos) {
    std::vector<CGenerator::NamedEnum> named_enums;
    for (const auto& enum_info : enum_infos) {
        std::string enum_name = LongName(enum_info.name);
        named_enums.push_back({std::move(enum_name), enum_info});
    }
    return named_enums;
}

std::vector<CGenerator::NamedMessage> CGenerator::NameInterfaces(const std::vector<flat::Interface>& interface_infos) {
    std::vector<CGenerator::NamedMessage> named_messages;
    for (const auto& interface_info : interface_infos) {
        for (const auto& method : interface_info.methods) {
            std::string name = LongName(interface_info.name) + ShortName(method.name);
            if (method.has_request) {
                std::string c_name = name + "Msg";
                std::string coded_name = name + "ReqCoded";
                named_messages.push_back({std::move(c_name), std::move(coded_name), method.maybe_request});
            }
            if (method.has_response) {
                if (!method.has_request) {
                    std::string c_name = name + "Evt";
                    std::string coded_name = name + "EvtCoded";
                    named_messages.push_back({std::move(c_name), std::move(coded_name), method.maybe_response});
                } else {
                    std::string c_name = name + "Rsp";
                    std::string coded_name = name + "RspCoded";
                    named_messages.push_back({std::move(c_name), std::move(coded_name), method.maybe_response});
                }
            }
        }
    }
    return named_messages;
}

std::vector<CGenerator::NamedStruct> CGenerator::NameStructs(const std::vector<flat::Struct>& struct_infos) {
    std::vector<CGenerator::NamedStruct> named_structs;
    for (const auto& struct_info : struct_infos) {
        std::string c_name = LongName(struct_info.name);
        std::string coded_name = LongName(struct_info.name) + "Coded";
        named_structs.push_back({std::move(c_name), std::move(coded_name), struct_info});
    }
    return named_structs;
}

std::vector<CGenerator::NamedUnion> CGenerator::NameUnions(const std::vector<flat::Union>& union_infos) {
    std::vector<CGenerator::NamedUnion> named_unions;
    for (const auto& union_info : union_infos) {
        std::string union_name = LongName(union_info.name);
        named_unions.push_back({std::move(union_name), union_info});
    }
    return named_unions;
}

void CGenerator::ProduceConstForwardDeclaration(const NamedConst& named_const) {
    // TODO(TO-702)
}

void CGenerator::ProduceEnumForwardDeclaration(const NamedEnum& named_enum) {
    IntegerConstantType literal_type = EnumType(named_enum.enum_info.type->subtype);
    GenerateIntegerTypedef(literal_type, named_enum.name);
    for (const auto& member : named_enum.enum_info.members) {
        std::string member_name = named_enum.name + "_" + LongName(member.name);
        std::string member_value;
        EnumValue(named_enum.enum_info.type->subtype, member.value.get(),
                  library_, &member_value);
        GenerateIntegerDefine(member_name, literal_type, std::move(member_value));
    }

    EmitBlank(&header_file_);
}

void CGenerator::ProduceMessageForwardDeclaration(const NamedMessage& named_message) {
    GenerateStructTypedef(named_message.c_name);
}

void CGenerator::ProduceStructForwardDeclaration(const NamedStruct& named_struct) {
    GenerateStructTypedef(named_struct.c_name);
}

void CGenerator::ProduceUnionForwardDeclaration(const NamedUnion& named_union) {
    GenerateStructTypedef(named_union.name);
}

void CGenerator::ProduceMessageExternDeclaration(const NamedMessage& named_message) {
    header_file_ << "extern const fidl_type_t " << named_message.coded_name << ";\n";
}

void CGenerator::ProduceConstDeclaration(const NamedConst& named_const) {
    // TODO(TO-702)
    static_cast<void>(named_const);

    EmitBlank(&header_file_);
}

void CGenerator::ProduceMessageDeclaration(const NamedMessage& message) {
    std::vector<CGenerator::Member> members;
    members.reserve(1 + message.parameters.size());
    members.push_back(MessageHeader());
    for (const auto& parameter : message.parameters) {
        const ast::Type* parameter_type = parameter.type.get();
        auto parameter_name = ShortName(parameter.name);
        members.push_back(CreateMember(library_, parameter_type, parameter_name));
    }

    GenerateStructDeclaration(message.c_name, members);

    EmitBlank(&header_file_);
}

void CGenerator::ProduceStructDeclaration(const NamedStruct& named_struct) {
    std::vector<CGenerator::Member> members;
    members.reserve(named_struct.struct_info.members.size());
    for (const auto& struct_member : named_struct.struct_info.members) {
        const ast::Type* struct_member_type = struct_member.type.get();
        auto struct_member_name = ShortName(struct_member.name);
        members.push_back(CreateMember(library_, struct_member_type, struct_member_name));
    }

    GenerateStructDeclaration(named_struct.c_name, members);

    EmitBlank(&header_file_);
}

void CGenerator::ProduceUnionDeclaration(const NamedUnion& named_union) {
    std::vector<CGenerator::Member> members = GenerateMembers(library_, named_union.union_info.members);
    GenerateTaggedUnionDeclaration(named_union.name, members);

    uint32_t tag = 0u;
    for (const auto& member : named_union.union_info.members) {
        std::string tag_name = UnionTagName(named_union.name, member.name);
        auto union_tag_type = CGenerator::IntegerConstantType::kUint32;
        std::ostringstream value;
        value << tag;
        GenerateIntegerDefine(std::move(tag_name), union_tag_type, value.str());
        ++tag;
    }

    EmitBlank(&header_file_);
}

void CGenerator::ProduceCStructs(std::ostringstream* header_file_out) {

    GeneratePrologues();

    std::vector<NamedConst> named_consts = NameConsts(library_->const_declarations_);
    std::vector<NamedEnum> named_enums = NameEnums(library_->enum_declarations_);
    std::vector<NamedMessage> named_messages = NameInterfaces(library_->interface_declarations_);
    std::vector<NamedStruct> named_structs = NameStructs(library_->struct_declarations_);
    std::vector<NamedUnion> named_unions = NameUnions(library_->union_declarations_);

    header_file_ << "\n// Forward declarations\n\n";
    for (const auto& named_const : named_consts) {
        ProduceConstForwardDeclaration(named_const);
    }
    for (const auto& named_enum : named_enums) {
        ProduceEnumForwardDeclaration(named_enum);
    }
    for (const auto& named_message : named_messages) {
        ProduceMessageForwardDeclaration(named_message);
    }
    for (const auto& named_struct : named_structs) {
        ProduceStructForwardDeclaration(named_struct);
    }
    for (const auto& named_union : named_unions) {
        ProduceUnionForwardDeclaration(named_union);
    }

    // Only messages have extern fidl_type_t declarations.
    header_file_ << "\n// Extern declarations\n\n";
    for (const auto& named_message : named_messages) {
        ProduceMessageExternDeclaration(named_message);
    }

    header_file_ << "\n// Declarations\n\n";
    for (const auto& named_const : named_consts) {
        ProduceConstDeclaration(named_const);
    }
    // Enums can be entirely forward declared, as they have no
    // dependencies other than standard headers.
    for (const auto& message : named_messages) {
        ProduceMessageDeclaration(message);
    }
    for (const auto& named_struct : named_structs) {
        ProduceStructDeclaration(named_struct);
    }
    for (const auto& named_union : named_unions) {
        ProduceUnionDeclaration(named_union);
    }

    GenerateEpilogues();

    *header_file_out = std::move(header_file_);
}

} // namespace fidl
