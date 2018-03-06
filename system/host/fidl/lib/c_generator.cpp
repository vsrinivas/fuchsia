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

std::string ShortName(SourceLocation name) {
    // TODO(TO-704) C name escaping and ergonomics.
    return name.data();
}

std::string LongName(const flat::Name& name) {
    // TODO(TO-701) Handle complex names.
    return name.data();
}

std::string UnionTagName(StringView union_name,
                         SourceLocation member_name) {
    return std::string(union_name) + "_tag_" + ShortName(member_name);
}

std::string PrimitiveTypeName(const raw::PrimitiveType* type) {
    switch (type->subtype) {
    case types::PrimitiveSubtype::Int8:
        return "int8_t";
    case types::PrimitiveSubtype::Int16:
        return "int16_t";
    case types::PrimitiveSubtype::Int32:
        return "int32_t";
    case types::PrimitiveSubtype::Int64:
        return "int64_t";
    case types::PrimitiveSubtype::Uint8:
        return "uint8_t";
    case types::PrimitiveSubtype::Uint16:
        return "uint16_t";
    case types::PrimitiveSubtype::Uint32:
        return "uint32_t";
    case types::PrimitiveSubtype::Uint64:
        return "uint64_t";
    case types::PrimitiveSubtype::Bool:
        return "bool";
    case types::PrimitiveSubtype::Status:
        return "zx_status_t";
    case types::PrimitiveSubtype::Float32:
        return "float";
    case types::PrimitiveSubtype::Float64:
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

std::string TypeName(const raw::Type* type) {
    for (;;) {
        switch (type->kind) {
        case raw::Type::Kind::Handle:
        case raw::Type::Kind::Request:
            return "zx_handle_t";

        case raw::Type::Kind::Vector:
            return "fidl_vector_t";
        case raw::Type::Kind::String:
            return "fidl_string_t";

        case raw::Type::Kind::Primitive: {
            auto primitive_type = static_cast<const raw::PrimitiveType*>(type);
            return PrimitiveTypeName(primitive_type);
        }

        case raw::Type::Kind::Array: {
            auto array_type = static_cast<const raw::ArrayType*>(type);
            type = array_type->element_type.get();
            continue;
        }

        case raw::Type::Kind::Identifier: {
            auto identifier_type = static_cast<const raw::IdentifierType*>(type);
            // TODO(TO-701) Handle longer names.
            const auto& components = identifier_type->identifier->components;
            assert(components.size() == 1);
            std::string name = components[0]->location.data();
            if (identifier_type->nullability == types::Nullability::Nullable) {
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

CGenerator::IntegerConstantType EnumType(types::PrimitiveSubtype type) {
    switch (type) {
    case types::PrimitiveSubtype::Int8:
        return CGenerator::IntegerConstantType::kInt8;
    case types::PrimitiveSubtype::Int16:
        return CGenerator::IntegerConstantType::kInt16;
    case types::PrimitiveSubtype::Int32:
        return CGenerator::IntegerConstantType::kInt32;
    case types::PrimitiveSubtype::Int64:
        return CGenerator::IntegerConstantType::kInt64;
    case types::PrimitiveSubtype::Uint8:
        return CGenerator::IntegerConstantType::kUint8;
    case types::PrimitiveSubtype::Uint16:
        return CGenerator::IntegerConstantType::kUint16;
    case types::PrimitiveSubtype::Uint32:
        return CGenerator::IntegerConstantType::kUint32;
    case types::PrimitiveSubtype::Uint64:
        return CGenerator::IntegerConstantType::kUint64;
    case types::PrimitiveSubtype::Bool:
    case types::PrimitiveSubtype::Status:
    case types::PrimitiveSubtype::Float32:
    case types::PrimitiveSubtype::Float64:
    default:
        assert(false && "bad primitive type for an enum");
        break;
    }
}

void EnumValue(types::PrimitiveSubtype type, const raw::Constant* constant,
               flat::Library* library, std::string* out_value) {
    // TODO(kulakowski) Move this into library resolution.

    std::ostringstream member_value;

    switch (type) {
    case types::PrimitiveSubtype::Int8: {
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
    case types::PrimitiveSubtype::Int16: {
        int16_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        member_value << value;
        break;
    }
    case types::PrimitiveSubtype::Int32: {
        int32_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        member_value << value;
        break;
    }
    case types::PrimitiveSubtype::Int64: {
        int64_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        member_value << value;
        break;
    }
    case types::PrimitiveSubtype::Uint8: {
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
    case types::PrimitiveSubtype::Uint16: {
        uint16_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        member_value << value;
        break;
    }
    case types::PrimitiveSubtype::Uint32: {
        uint32_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        member_value << value;
        break;
    }
    case types::PrimitiveSubtype::Uint64: {
        uint64_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        member_value << value;
        break;
    }
    case types::PrimitiveSubtype::Bool:
    case types::PrimitiveSubtype::Status:
    case types::PrimitiveSubtype::Float32:
    case types::PrimitiveSubtype::Float64:
        assert(false && "bad primitive type for an enum");
        break;
    }

    *out_value = member_value.str();
}

std::vector<uint32_t> ArrayCounts(flat::Library* library, const raw::Type* type) {
    std::vector<uint32_t> array_counts;
    for (;;) {
        switch (type->kind) {
        default: { return array_counts; }
        case raw::Type::Kind::Array: {
            auto array_type = static_cast<const raw::ArrayType*>(type);
            const raw::Constant* count_constant = array_type->element_count.get();
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

CGenerator::Member CreateMember(flat::Library* library, const raw::Type* type, StringView name) {
    auto type_name = TypeName(type);
    std::vector<uint32_t> array_counts = ArrayCounts(library, type);
    return CGenerator::Member{type_name, name, std::move(array_counts)};
}

std::vector<CGenerator::Member>
GenerateMembers(flat::Library* library, const std::vector<flat::Union::Member>& union_members) {
    std::vector<CGenerator::Member> members;
    members.reserve(union_members.size());
    for (const auto& union_member : union_members) {
        const raw::Type* union_member_type = union_member.type.get();
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
std::map<const flat::Decl*, CGenerator::NamedConst> CGenerator::NameConsts(const std::vector<std::unique_ptr<flat::Const>>& const_infos) {
    std::map<const flat::Decl*, NamedConst> named_consts;
    for (const auto& const_info : const_infos) {
        named_consts.emplace(const_info.get(), NamedConst{"", *const_info});
    }
    return named_consts;
}

std::map<const flat::Decl*, CGenerator::NamedEnum> CGenerator::NameEnums(const std::vector<std::unique_ptr<flat::Enum>>& enum_infos) {
    std::map<const flat::Decl*, NamedEnum> named_enums;
    for (const auto& enum_info : enum_infos) {
        std::string enum_name = LongName(enum_info->name);
        named_enums.emplace(enum_info.get(), NamedEnum{std::move(enum_name), *enum_info});
    }
    return named_enums;
}

std::map<const flat::Decl*, CGenerator::NamedInterface> CGenerator::NameInterfaces(const std::vector<std::unique_ptr<flat::Interface>>& interface_infos) {
    std::map<const flat::Decl*, NamedInterface> named_interfaces;
    for (const auto& interface_info : interface_infos) {
        NamedInterface named_interface;
        for (const auto& method : interface_info->methods) {
            NamedMethod named_method;
            std::string name = LongName(interface_info->name) + ShortName(method.name);
            if (method.maybe_request != nullptr) {
                std::string c_name = name + "Msg";
                std::string coded_name = name + "ReqCoded";
                named_method.request = std::make_unique<NamedMessage>(NamedMessage{std::move(c_name), std::move(coded_name), method.maybe_request->parameters});
            }
            if (method.maybe_response != nullptr) {
                if (method.maybe_request == nullptr) {
                    std::string c_name = name + "Evt";
                    std::string coded_name = name + "EvtCoded";
                    named_method.response = std::make_unique<NamedMessage>(NamedMessage{std::move(c_name), std::move(coded_name), method.maybe_response->parameters});
                } else {
                    std::string c_name = name + "Rsp";
                    std::string coded_name = name + "RspCoded";
                    named_method.response = std::make_unique<NamedMessage>(NamedMessage{std::move(c_name), std::move(coded_name), method.maybe_response->parameters});
                }
            }
            named_interface.methods.push_back(std::move(named_method));
        }
        named_interfaces.emplace(interface_info.get(), std::move(named_interface));
    }
    return named_interfaces;
}

std::map<const flat::Decl*, CGenerator::NamedStruct> CGenerator::NameStructs(const std::vector<std::unique_ptr<flat::Struct>>& struct_infos) {
    std::map<const flat::Decl*, NamedStruct> named_structs;
    for (const auto& struct_info : struct_infos) {
        std::string c_name = LongName(struct_info->name);
        std::string coded_name = LongName(struct_info->name) + "Coded";
        named_structs.emplace(struct_info.get(), NamedStruct{std::move(c_name), std::move(coded_name), *struct_info});
    }
    return named_structs;
}

std::map<const flat::Decl*, CGenerator::NamedUnion> CGenerator::NameUnions(const std::vector<std::unique_ptr<flat::Union>>& union_infos) {
    std::map<const flat::Decl*, NamedUnion> named_unions;
    for (const auto& union_info : union_infos) {
        std::string union_name = LongName(union_info->name);
        named_unions.emplace(union_info.get(), NamedUnion{std::move(union_name), *union_info});
    }
    return named_unions;
}

void CGenerator::ProduceConstForwardDeclaration(const NamedConst& named_const) {
    // TODO(TO-702)
}

void CGenerator::ProduceEnumForwardDeclaration(const NamedEnum& named_enum) {
    IntegerConstantType literal_type = EnumType(named_enum.enum_info.type);
    GenerateIntegerTypedef(literal_type, named_enum.name);
    for (const auto& member : named_enum.enum_info.members) {
        std::string member_name = named_enum.name + "_" + LongName(member.name);
        std::string member_value;
        EnumValue(named_enum.enum_info.type, member.value.get(),
                  library_, &member_value);
        GenerateIntegerDefine(member_name, literal_type, std::move(member_value));
    }

    EmitBlank(&header_file_);
}

void CGenerator::ProduceInterfaceForwardDeclaration(const NamedInterface& named_interface) {
    for (const auto& method_info : named_interface.methods) {
        if (method_info.request)
            GenerateStructTypedef(method_info.request->c_name);
        if (method_info.response)
            GenerateStructTypedef(method_info.response->c_name);
    }
}

void CGenerator::ProduceStructForwardDeclaration(const NamedStruct& named_struct) {
    GenerateStructTypedef(named_struct.c_name);
}

void CGenerator::ProduceUnionForwardDeclaration(const NamedUnion& named_union) {
    GenerateStructTypedef(named_union.name);
}

void CGenerator::ProduceInterfaceExternDeclaration(const NamedInterface& named_interface) {
    for (const auto& method_info : named_interface.methods) {
        if (method_info.request)
            header_file_ << "extern const fidl_type_t " << method_info.request->coded_name << ";\n";
        if (method_info.response)
            header_file_ << "extern const fidl_type_t " << method_info.response->coded_name << ";\n";
    }
}

void CGenerator::ProduceConstDeclaration(const NamedConst& named_const) {
    // TODO(TO-702)
    static_cast<void>(named_const);

    EmitBlank(&header_file_);
}

void CGenerator::ProduceMessageDeclaration(const NamedMessage& named_message) {
    std::vector<CGenerator::Member> members;
    members.reserve(1 + named_message.parameters.size());
    members.push_back(MessageHeader());
    for (const auto& parameter : named_message.parameters) {
        const raw::Type* parameter_type = parameter.type.get();
        auto parameter_name = ShortName(parameter.name);
        members.push_back(CreateMember(library_, parameter_type, parameter_name));
    }

    GenerateStructDeclaration(named_message.c_name, members);

    EmitBlank(&header_file_);
}

void CGenerator::ProduceInterfaceDeclaration(const NamedInterface& named_interface) {
    for (const auto& method_info : named_interface.methods) {
        if (method_info.request)
            ProduceMessageDeclaration(*method_info.request);
        if (method_info.response)
            ProduceMessageDeclaration(*method_info.response);
    }
}

void CGenerator::ProduceStructDeclaration(const NamedStruct& named_struct) {
    std::vector<CGenerator::Member> members;
    members.reserve(named_struct.struct_info.members.size());
    for (const auto& struct_member : named_struct.struct_info.members) {
        const raw::Type* struct_member_type = struct_member.type.get();
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

std::ostringstream CGenerator::Produce() {
    GeneratePrologues();

    std::map<const flat::Decl*, NamedConst> named_consts = NameConsts(library_->const_declarations_);
    std::map<const flat::Decl*, NamedEnum> named_enums = NameEnums(library_->enum_declarations_);
    std::map<const flat::Decl*, NamedInterface> named_interfaces = NameInterfaces(library_->interface_declarations_);
    std::map<const flat::Decl*, NamedStruct> named_structs = NameStructs(library_->struct_declarations_);
    std::map<const flat::Decl*, NamedUnion> named_unions = NameUnions(library_->union_declarations_);

    header_file_ << "\n// Forward declarations\n\n";

    for (const auto* decl : library_->declaration_order_) {
        switch (decl->kind) {
        case flat::Decl::Kind::kConst:
            ProduceConstForwardDeclaration(named_consts.find(decl)->second);
            break;
        case flat::Decl::Kind::kEnum:
            ProduceEnumForwardDeclaration(named_enums.find(decl)->second);
            break;
        case flat::Decl::Kind::kInterface:
            ProduceInterfaceForwardDeclaration(named_interfaces.find(decl)->second);
            break;
        case flat::Decl::Kind::kStruct:
            ProduceStructForwardDeclaration(named_structs.find(decl)->second);
            break;
        case flat::Decl::Kind::kUnion:
            ProduceUnionForwardDeclaration(named_unions.find(decl)->second);
            break;
        default:
            abort();
        }
    }

    header_file_ << "\n// Extern declarations\n\n";

    for (const auto* decl : library_->declaration_order_) {
        switch (decl->kind) {
        case flat::Decl::Kind::kConst:
        case flat::Decl::Kind::kEnum:
        case flat::Decl::Kind::kStruct:
        case flat::Decl::Kind::kUnion:
            // Only messages have extern fidl_type_t declarations.
            break;
        case flat::Decl::Kind::kInterface:
            ProduceInterfaceExternDeclaration(named_interfaces.find(decl)->second);
            break;
        default:
            abort();
        }
    }

    header_file_ << "\n// Declarations\n\n";

    for (const auto* decl : library_->declaration_order_) {
        switch (decl->kind) {
        case flat::Decl::Kind::kConst:
            ProduceConstDeclaration(named_consts.find(decl)->second);
            break;
        case flat::Decl::Kind::kEnum:
            // Enums can be entirely forward declared, as they have no
            // dependencies other than standard headers.
            break;
        case flat::Decl::Kind::kInterface:
            ProduceInterfaceDeclaration(named_interfaces.find(decl)->second);
            break;
        case flat::Decl::Kind::kStruct:
            ProduceStructDeclaration(named_structs.find(decl)->second);
            break;
        case flat::Decl::Kind::kUnion:
            ProduceUnionDeclaration(named_unions.find(decl)->second);
            break;
        default:
            abort();
        }
    }

    GenerateEpilogues();

    return std::move(header_file_);
}

} // namespace fidl
