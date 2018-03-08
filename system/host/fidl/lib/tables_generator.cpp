// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/tables_generator.h"

namespace fidl {

namespace {

constexpr auto kIndent = "    ";

std::string LongName(const flat::Name& name) {
    // TODO(TO-701) Handle complex names.
    return name.data();
}

std::ostream& operator<<(std::ostream& stream, StringView view) {
    stream.rdbuf()->sputn(view.data(), view.size());
    return stream;
}

void Emit(std::ostream* file, StringView data) {
    *file << data;
}

void EmitNewline(std::ostream* file) {
    *file << "\n";
}

void EmitNewlineAndIndent(std::ostream* file, size_t indent_level) {
    *file << "\n";
    while (indent_level--)
        *file << kIndent;
}

void EmitArrayBegin(std::ostream* file) {
    *file << "{";
}

void EmitArraySeparator(std::ostream* file, size_t indent_level) {
    *file << ",";
    EmitNewlineAndIndent(file, indent_level);
}

void EmitArrayEnd(std::ostream* file) {
    *file << "}";
}

void Emit(std::ostream* file, uint32_t value) {
    *file << value;
}

void Emit(std::ostream* file, types::HandleSubtype handle_subtype) {
    switch (handle_subtype) {
    case types::HandleSubtype::Handle:
        Emit(file, "ZX_OBJ_TYPE_NONE");
        break;
    case types::HandleSubtype::Process:
        Emit(file, "ZX_OBJ_TYPE_PROCESS");
        break;
    case types::HandleSubtype::Thread:
        Emit(file, "ZX_OBJ_TYPE_THREAD");
        break;
    case types::HandleSubtype::Vmo:
        Emit(file, "ZX_OBJ_TYPE_VMO");
        break;
    case types::HandleSubtype::Channel:
        Emit(file, "ZX_OBJ_TYPE_CHANNEL");
        break;
    case types::HandleSubtype::Event:
        Emit(file, "ZX_OBJ_TYPE_EVENT");
        break;
    case types::HandleSubtype::Port:
        Emit(file, "ZX_OBJ_TYPE_PORT");
        break;
    case types::HandleSubtype::Interrupt:
        Emit(file, "ZX_OBJ_TYPE_INTERRUPT");
        break;
    case types::HandleSubtype::Log:
        Emit(file, "ZX_OBJ_TYPE_LOG");
        break;
    case types::HandleSubtype::Socket:
        Emit(file, "ZX_OBJ_TYPE_SOCKET");
        break;
    case types::HandleSubtype::Resource:
        Emit(file, "ZX_OBJ_TYPE_RESOURCE");
        break;
    case types::HandleSubtype::Eventpair:
        Emit(file, "ZX_OBJ_TYPE_EVENT_PAIR");
        break;
    case types::HandleSubtype::Job:
        Emit(file, "ZX_OBJ_TYPE_JOB");
        break;
    case types::HandleSubtype::Vmar:
        Emit(file, "ZX_OBJ_TYPE_VMAR");
        break;
    case types::HandleSubtype::Fifo:
        Emit(file, "ZX_OBJ_TYPE_FIFO");
        break;
    case types::HandleSubtype::Guest:
        Emit(file, "ZX_OBJ_TYPE_GUEST");
        break;
    case types::HandleSubtype::Timer:
        Emit(file, "ZX_OBJ_TYPE_TIMER");
        break;
    }
}

void Emit(std::ostream* file, types::Nullability nullability) {
    switch (nullability) {
    case types::Nullability::Nullable:
        Emit(file, "::fidl::kNullable");
        break;
    case types::Nullability::Nonnullable:
        Emit(file, "::fidl::kNonnullable");
        break;
    }
}

const char* NullabilityName(types::Nullability nullability) {
    switch (nullability) {
    case types::Nullability::Nonnullable:
        return "nonnullable";
    case types::Nullability::Nullable:
        return "nullable";
    }
}

const char* HandleSubtypeName(types::HandleSubtype subtype) {
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

std::string SizeName(uint64_t size) {
    if (size == std::numeric_limits<uint64_t>::max())
        return "unbounded";
    std::ostringstream name;
    name << size;
    return name.str();
}

std::string PrimitiveName(types::PrimitiveSubtype subtype) {
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

std::string HandleName(types::HandleSubtype subtype, types::Nullability nullability) {
    std::string name("handle");
    name += "_";
    name += HandleSubtypeName(subtype);
    name += "_";
    name += NullabilityName(nullability);
    return name;
}

std::string InterfaceHandleName(StringView interface_name, types::Nullability nullability) {
    std::string name("interface");
    name += "_";
    name += interface_name;
    name += "_";
    name += NullabilityName(nullability);
    return name;
}

std::string RequestHandleName(StringView interface_name, types::Nullability nullability) {
    std::string name("request");
    name += "_";
    name += interface_name;
    name += "_";
    name += NullabilityName(nullability);
    return name;
}

std::string ArrayName(StringView element_name, uint64_t size) {
    std::string name("array");
    name += "_";
    name += element_name;
    name += "_";
    name += SizeName(size);
    return name;
}

std::string VectorName(StringView element_name, uint64_t max_size, types::Nullability nullability) {
    std::string name("vector");
    name += "_";
    name += element_name;
    name += "_";
    name += SizeName(max_size);
    name += "_";
    name += NullabilityName(nullability);
    return name;
}

std::string StringName(uint64_t max_size, types::Nullability nullability) {
    std::string name("string");
    name += "_";
    name += SizeName(max_size);
    name += "_";
    name += NullabilityName(nullability);
    return name;
}

std::string TypeName(StringView name) {
    std::string type_name(name);
    type_name += "_type";
    return type_name;
}

std::string PointerName(StringView name) {
    std::string pointer_name(name);
    pointer_name += "_pointer";
    return TypeName(pointer_name);
}

std::string MembersName(StringView name) {
    std::string members_name(name);
    members_name += "_members";
    return TypeName(members_name);
}

std::string FieldsName(StringView name) {
    std::string fields_name(name);
    fields_name += "_fields";
    return fields_name;
}

} // namespace

void TablesGenerator::GenerateInclude(StringView filename) {
    Emit(&tables_file_, "#include ");
    Emit(&tables_file_, filename);
    EmitNewline(&tables_file_);
}

void TablesGenerator::GenerateFilePreamble() {
    GenerateInclude("<fidl/internal.h>");
    Emit(&tables_file_, "extern \"C\" {\n");
    EmitNewline(&tables_file_);
}

void TablesGenerator::GenerateFilePostamble() {
    Emit(&tables_file_, "} // extern \"C\"\n");
    EmitNewline(&tables_file_);
}

template <typename Collection>
void TablesGenerator::GenerateArray(const Collection& collection) {
    EmitArrayBegin(&tables_file_);

    if (!collection.empty())
        EmitNewlineAndIndent(&tables_file_, ++indent_level_);

    for (size_t i = 0; i < collection.size(); ++i) {
        if (i)
            EmitArraySeparator(&tables_file_, indent_level_);
        Generate(collection[i]);
    }

    if (!collection.empty())
        EmitNewlineAndIndent(&tables_file_, --indent_level_);

    EmitArrayEnd(&tables_file_);
}

void TablesGenerator::Generate(const coded::StructType& struct_type) {
    Emit(&tables_file_, "extern const fidl_type_t ");
    Emit(&tables_file_, TypeName(struct_type.coded_name));
    Emit(&tables_file_, ";\n");

    Emit(&tables_file_, "static const ::fidl::FidlField ");
    Emit(&tables_file_, FieldsName(struct_type.coded_name));
    Emit(&tables_file_, "[] = ");
    GenerateArray(struct_type.fields);
    Emit(&tables_file_, ";\n");

    Emit(&tables_file_, "const fidl_type_t ");
    Emit(&tables_file_, TypeName(struct_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedStruct(");
    Emit(&tables_file_, FieldsName(struct_type.coded_name));
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, struct_type.fields.size());
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, struct_type.size);
    Emit(&tables_file_, "));\n\n");

    if (struct_type.referenced_by_pointer) {
        Emit(&tables_file_, "extern const fidl_type_t ");
        Emit(&tables_file_, PointerName(struct_type.coded_name));
        Emit(&tables_file_, ";\n");

        Emit(&tables_file_, "const fidl_type_t ");
        Emit(&tables_file_, PointerName(struct_type.coded_name));
        Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedStructPointer(&");
        Emit(&tables_file_, TypeName(struct_type.coded_name));
        Emit(&tables_file_, ".coded_struct));\n\n");
    }
}

void TablesGenerator::Generate(const coded::UnionType& union_type) {
    Emit(&tables_file_, "extern const fidl_type_t ");
    Emit(&tables_file_, TypeName(union_type.coded_name));
    Emit(&tables_file_, ";\n");

    Emit(&tables_file_, "static const fidl_type_t* ");
    Emit(&tables_file_, MembersName(union_type.coded_name));
    Emit(&tables_file_, "[] = ");
    GenerateArray(union_type.types);
    Emit(&tables_file_, ";\n");

    Emit(&tables_file_, "const fidl_type_t ");
    Emit(&tables_file_, TypeName(union_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedUnion(");
    Emit(&tables_file_, MembersName(union_type.coded_name));
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, union_type.types.size());
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, union_type.data_offset);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, union_type.size);
    Emit(&tables_file_, "));\n\n");

    if (union_type.referenced_by_pointer) {
        Emit(&tables_file_, "extern const fidl_type_t ");
        Emit(&tables_file_, PointerName(union_type.coded_name));
        Emit(&tables_file_, ";\n");

        Emit(&tables_file_, "const fidl_type_t ");
        Emit(&tables_file_, PointerName(union_type.coded_name));
        Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedUnionPointer(&");
        Emit(&tables_file_, TypeName(union_type.coded_name));
        Emit(&tables_file_, ".coded_union));\n\n");
    }
}

void TablesGenerator::Generate(const coded::HandleType& handle_type) {
    Emit(&tables_file_, "static const fidl_type_t ");
    Emit(&tables_file_, TypeName(handle_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedHandle(");
    Emit(&tables_file_, handle_type.subtype);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, handle_type.nullability);
    Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::RequestHandleType& request_type) {
    Emit(&tables_file_, "static const fidl_type_t ");
    Emit(&tables_file_, TypeName(request_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedHandle(");
    Emit(&tables_file_, types::HandleSubtype::Channel);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, request_type.nullability);
    Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::InterfaceHandleType& interface_type) {
    Emit(&tables_file_, "static const fidl_type_t ");
    Emit(&tables_file_, TypeName(interface_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedHandle(");
    Emit(&tables_file_, types::HandleSubtype::Channel);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, interface_type.nullability);
    Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::ArrayType& array_type) {
    Emit(&tables_file_, "static const fidl_type_t ");
    Emit(&tables_file_, TypeName(array_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedArray(&");
    Emit(&tables_file_, TypeName(array_type.element_type->coded_name));
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, array_type.array_size);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, array_type.element_size);
    Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::StringType& string_type) {
    Emit(&tables_file_, "static const fidl_type_t ");
    Emit(&tables_file_, TypeName(string_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedString(");
    Emit(&tables_file_, string_type.max_size);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, string_type.nullability);
    Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::VectorType& vector_type) {
    Emit(&tables_file_, "static const fidl_type_t ");
    Emit(&tables_file_, TypeName(vector_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedVector(");
    if (vector_type.element_type->coding_needed == coded::CodingNeeded::kNeeded) {
        Emit(&tables_file_, "&");
        Emit(&tables_file_, TypeName(vector_type.element_type->coded_name));
    } else {
        Emit(&tables_file_, "nullptr");
    }
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, vector_type.max_count);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, vector_type.element_size);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, vector_type.nullability);
    Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::Type* type) {
    Emit(&tables_file_, "&");
    Emit(&tables_file_, TypeName(type->coded_name));
}

void TablesGenerator::Generate(const coded::Field& field) {
    Emit(&tables_file_, "::fidl::FidlField(&");
    Emit(&tables_file_, TypeName(field.type->coded_name));
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, field.offset);
    Emit(&tables_file_, ")");
}

const coded::Type* TablesGenerator::Compile(const flat::Type* type) {
    switch (type->kind) {
    case flat::Type::Kind::Array: {
        auto array_type = static_cast<const flat::ArrayType*>(type);
        auto iter = array_type_map_.find(array_type);
        if (iter != array_type_map_.end())
            return iter->second;
        auto coded_element_type = Compile(array_type->element_type.get());
        uint32_t array_size = array_type->size;
        uint32_t element_size = array_type->element_type->size;
        auto name = ArrayName(coded_element_type->coded_name, array_size);
        auto coded_array_type = std::make_unique<coded::ArrayType>(std::move(name), coded_element_type, array_size, element_size);
        array_type_map_[array_type] = coded_array_type.get();
        coded_types_.push_back(std::move(coded_array_type));
        return coded_types_.back().get();
    }
    case flat::Type::Kind::Vector: {
        auto vector_type = static_cast<const flat::VectorType*>(type);
        auto iter = vector_type_map_.find(vector_type);
        if (iter != vector_type_map_.end())
            return iter->second;
        auto coded_element_type = Compile(vector_type->element_type.get());
        uint64_t max_count = vector_type->element_count.Value();
        uint64_t element_size = vector_type->element_type->size;
        StringView element_name = coded_element_type->coded_name;
        auto name = VectorName(element_name, max_count, vector_type->nullability);
        auto coded_vector_type = std::make_unique<coded::VectorType>(std::move(name), coded_element_type, max_count, element_size, vector_type->nullability);
        vector_type_map_[vector_type] = coded_vector_type.get();
        coded_types_.push_back(std::move(coded_vector_type));
        return coded_types_.back().get();
    }
    case flat::Type::Kind::String: {
        auto string_type = static_cast<const flat::StringType*>(type);
        auto iter = string_type_map_.find(string_type);
        if (iter != string_type_map_.end())
            return iter->second;
        uint32_t max_size = string_type->max_size.Value();
        auto name = StringName(max_size, string_type->nullability);
        auto coded_string_type = std::make_unique<coded::StringType>(std::move(name), max_size, string_type->nullability);
        string_type_map_[string_type] = coded_string_type.get();
        coded_types_.push_back(std::move(coded_string_type));
        return coded_types_.back().get();
    }
    case flat::Type::Kind::Handle: {
        auto handle_type = static_cast<const flat::HandleType*>(type);
        auto iter = handle_type_map_.find(handle_type);
        if (iter != handle_type_map_.end())
            return iter->second;
        auto name = HandleName(handle_type->subtype, handle_type->nullability);
        auto coded_handle_type = std::make_unique<coded::HandleType>(std::move(name), handle_type->subtype, handle_type->nullability);
        handle_type_map_[handle_type] = coded_handle_type.get();
        coded_types_.push_back(std::move(coded_handle_type));
        return coded_types_.back().get();
    }
    case flat::Type::Kind::RequestHandle: {
        auto request_type = static_cast<const flat::RequestHandleType*>(type);
        auto iter = request_type_map_.find(request_type);
        if (iter != request_type_map_.end())
            return iter->second;
        auto name = RequestHandleName(LongName(request_type->name), request_type->nullability);
        auto coded_request_type = std::make_unique<coded::RequestHandleType>(std::move(name), request_type->nullability);
        request_type_map_[request_type] = coded_request_type.get();
        coded_types_.push_back(std::move(coded_request_type));
        return coded_types_.back().get();
    }
    case flat::Type::Kind::Primitive: {
        auto primitive_type = static_cast<const flat::PrimitiveType*>(type);
        auto iter = primitive_type_map_.find(primitive_type);
        if (iter != primitive_type_map_.end())
            return iter->second;
        auto name = PrimitiveName(primitive_type->subtype);
        auto coded_primitive_type = std::make_unique<coded::PrimitiveType>(std::move(name), primitive_type->subtype);
        primitive_type_map_[primitive_type] = coded_primitive_type.get();
        coded_types_.push_back(std::move(coded_primitive_type));
        return coded_types_.back().get();
    }
    case flat::Type::Kind::Identifier: {
        auto identifier_type = static_cast<const flat::IdentifierType*>(type);
        auto iter = named_type_map_.find(&identifier_type->name);
        if (iter == named_type_map_.end()) {
            // This must be an interface we haven't seen yet.
            auto name = InterfaceHandleName(LongName(identifier_type->name), identifier_type->nullability);
            coded_types_.push_back(std::make_unique<coded::InterfaceHandleType>(std::move(name), identifier_type->nullability));
            auto coded_type = coded_types_.back().get();
            named_type_map_[&identifier_type->name] = coded_type;
            return coded_type;
        }
        // Otherwise we have seen this type before. But we may need to
        // set the emit-pointer bit on structs and unions, now.
        auto coded_type = iter->second;
        if (identifier_type->nullability == types::Nullability::Nullable) {
            switch (coded_type->kind) {
            case coded::Type::Kind::kStruct:
                // Structs were compiled as part of decl compilation,
                // but we may now need to generate the StructPointer.
                static_cast<coded::StructType*>(coded_type)->referenced_by_pointer = true;
                break;
            case coded::Type::Kind::kUnion:
                // Unions were compiled as part of decl compilation,
                // but we may now need to generate the UnionPointer.
                static_cast<coded::UnionType*>(coded_type)->referenced_by_pointer = true;
                break;
            case coded::Type::Kind::kInterfaceHandle:
                break;
            case coded::Type::Kind::kPrimitive:
            case coded::Type::Kind::kRequestHandle:
            case coded::Type::Kind::kHandle:
            case coded::Type::Kind::kArray:
            case coded::Type::Kind::kVector:
            case coded::Type::Kind::kString:
                assert(false && "anonymous type in named type map!");
                break;
            }
        }
        return coded_type;
    }
    }
}

void TablesGenerator::Compile(const flat::Decl* decl) {
    switch (decl->kind) {
    case flat::Decl::Kind::kConst:
    case flat::Decl::Kind::kEnum:
        // Nothing to do for const or enum declarations.
        break;
    case flat::Decl::Kind::kInterface: {
        auto interface_decl = static_cast<const flat::Interface*>(decl);
        std::string interface_name = LongName(interface_decl->name);
        for (const auto& method : interface_decl->methods) {
            std::string method_name = interface_name + "_" + std::string(method.name.data());
            if (method.maybe_request) {
                std::vector<coded::Field> request_fields;
                std::string request_name = method_name + "_RequestHandle";
                for (const auto& parameter : method.maybe_request->parameters) {
                    std::string parameter_name = request_name + "_" + std::string(parameter.name.data());
                    auto coded_parameter_type = Compile(parameter.type.get());
                    if (coded_parameter_type->coding_needed == coded::CodingNeeded::kNeeded)
                        request_fields.emplace_back(coded_parameter_type, parameter.fieldshape.Offset());
                }
                coded_types_.push_back(std::make_unique<coded::StructType>(std::move(request_name), std::move(request_fields), method.maybe_request->typeshape.Size()));
            }
            if (method.maybe_response) {
                std::string response_name = method_name + (method.maybe_request ? "_Response" : "_Event");
                std::vector<coded::Field> response_fields;
                for (const auto& parameter : method.maybe_response->parameters) {
                    std::string parameter_name = response_name + "_" + std::string(parameter.name.data());
                    auto coded_parameter_type = Compile(parameter.type.get());
                    if (coded_parameter_type->coding_needed == coded::CodingNeeded::kNeeded)
                        response_fields.emplace_back(coded_parameter_type, parameter.fieldshape.Offset());
                }
                coded_types_.push_back(std::make_unique<coded::StructType>(std::move(response_name), std::move(response_fields), method.maybe_response->typeshape.Size()));
            }
        }
        break;
    }
    case flat::Decl::Kind::kStruct: {
        auto struct_decl = static_cast<const flat::Struct*>(decl);
        std::string struct_name = LongName(struct_decl->name);
        std::vector<coded::Field> struct_fields;
        for (const auto& member : struct_decl->members) {
            std::string member_name = struct_name + "_" + std::string(member.name.data());
            auto coded_member_type = Compile(member.type.get());
            if (coded_member_type->coding_needed == coded::CodingNeeded::kNeeded)
                struct_fields.emplace_back(coded_member_type, member.fieldshape.Offset());
        }
        coded_types_.push_back(std::make_unique<coded::StructType>(std::move(struct_name), std::move(struct_fields), struct_decl->typeshape.Size()));
        named_type_map_[&decl->name] = coded_types_.back().get();
        break;
    }
    case flat::Decl::Kind::kUnion: {
        auto union_decl = static_cast<const flat::Union*>(decl);
        std::string union_name = LongName(union_decl->name);
        std::vector<const coded::Type*> union_members;
        for (const auto& member : union_decl->members) {
            std::string member_name = union_name + "_" + std::string(member.name.data());
            auto coded_member_type = Compile(member.type.get());
            if (coded_member_type->coding_needed == coded::CodingNeeded::kNeeded)
                union_members.push_back(coded_member_type);
        }
        coded_types_.push_back(std::make_unique<coded::UnionType>(std::move(union_name), std::move(union_members),
                                                                  union_decl->fieldshape.Offset(), union_decl->fieldshape.Size()));
        named_type_map_[&decl->name] = coded_types_.back().get();
        break;
    }
    }
}

std::ostringstream TablesGenerator::Produce() {
    GenerateFilePreamble();

    for (const auto& decl : library_->declaration_order_)
        Compile(decl);

    for (const auto& coded_type : coded_types_) {
        if (coded_type->coding_needed == coded::CodingNeeded::kNotNeeded)
            continue;

        switch (coded_type->kind) {
        case coded::Type::Kind::kStruct:
            Generate(*static_cast<const coded::StructType*>(coded_type.get()));
            break;
        case coded::Type::Kind::kUnion:
            Generate(*static_cast<const coded::UnionType*>(coded_type.get()));
            break;
        case coded::Type::Kind::kHandle:
            Generate(*static_cast<const coded::HandleType*>(coded_type.get()));
            break;
        case coded::Type::Kind::kInterfaceHandle:
            Generate(*static_cast<const coded::InterfaceHandleType*>(coded_type.get()));
            break;
        case coded::Type::Kind::kRequestHandle:
            Generate(*static_cast<const coded::RequestHandleType*>(coded_type.get()));
            break;
        case coded::Type::Kind::kArray:
            Generate(*static_cast<const coded::ArrayType*>(coded_type.get()));
            break;
        case coded::Type::Kind::kString:
            Generate(*static_cast<const coded::StringType*>(coded_type.get()));
            break;
        case coded::Type::Kind::kVector:
            Generate(*static_cast<const coded::VectorType*>(coded_type.get()));
            break;
        case coded::Type::Kind::kPrimitive:
            // These are only around to provide size information to
            // vectors. There's never anything to generate, and this
            // should not be reached.
            assert(false && "Primitive types should never need coding tables");
            break;
        }
    }

    GenerateFilePostamble();

    return std::move(tables_file_);
}

} // namespace fidl
