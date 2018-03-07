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
        Emit(file, "ZX_OBJ_TYPE_EVENTPAIR");
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
        Emit(file, "kNullable");
        break;
    case types::Nullability::Nonnullable:
        Emit(file, "kNonnullable");
        break;
    }
}

std::string HandleName(StringView name, types::HandleSubtype subtype, types::Nullability nullability) {
    std::ostringstream handle_name;
    handle_name << name << "_";
    switch (nullability) {
    case types::Nullability::Nullable:
        handle_name << "nullable";
        break;
    case types::Nullability::Nonnullable:
        handle_name << "nonnullable";
        break;
    }
    handle_name << "_";
    switch (subtype) {
    case types::HandleSubtype::Handle:
        handle_name << "handle";
        break;
    case types::HandleSubtype::Process:
        handle_name << "process";
        break;
    case types::HandleSubtype::Thread:
        handle_name << "thread";
        break;
    case types::HandleSubtype::Vmo:
        handle_name << "vmo";
        break;
    case types::HandleSubtype::Channel:
        handle_name << "channel";
        break;
    case types::HandleSubtype::Event:
        handle_name << "event";
        break;
    case types::HandleSubtype::Port:
        handle_name << "port";
        break;
    case types::HandleSubtype::Interrupt:
        handle_name << "interrupt";
        break;
    case types::HandleSubtype::Log:
        handle_name << "log";
        break;
    case types::HandleSubtype::Socket:
        handle_name << "socket";
        break;
    case types::HandleSubtype::Resource:
        handle_name << "resource";
        break;
    case types::HandleSubtype::Eventpair:
        handle_name << "eventpair";
        break;
    case types::HandleSubtype::Job:
        handle_name << "job";
        break;
    case types::HandleSubtype::Vmar:
        handle_name << "vmar";
        break;
    case types::HandleSubtype::Fifo:
        handle_name << "fifo";
        break;
    case types::HandleSubtype::Guest:
        handle_name << "guest";
        break;
    case types::HandleSubtype::Timer:
        handle_name << "timer";
        break;
    }
    // TODO(TO-856) Deduplicate handle type names so that this is no
    // longer necessary.
    static uint64_t id;
    handle_name << "_" << id++;
    return handle_name.str();
}

std::string TypeName(StringView name) {
    std::string public_name(name);
    public_name += "_Type";
    return public_name;
}

std::string PointerName(StringView name) {
    std::string pointer_name(name);
    pointer_name += "_Pointer";
    return pointer_name;
}

std::string FieldsName(StringView name) {
    std::string fields_name(name);
    fields_name += "_Fields";
    return fields_name;
}

std::string MembersName(StringView name) {
    std::string members_name(name);
    members_name += "_Members";
    return members_name;
}

std::string ArrayName(StringView name) {
    std::ostringstream array_name;
    array_name << name << "_Array";
    // TODO(TO-856) Deduplicate array type names so that this is no
    // longer necessary.
    static uint64_t id;
    array_name << "_" << id++;
    return array_name.str();
}

std::string VectorName(StringView name) {
    std::ostringstream vector_name;
    vector_name << name << "_Vector";
    // TODO(TO-856) Deduplicate vector type names so that this is no
    // longer necessary.
    static uint64_t id;
    vector_name << "_" << id++;
    return vector_name.str();
}

std::string StringName(StringView name) {
    std::ostringstream string_name;
    string_name << name << "_String";
    // TODO(TO-856) Deduplicate string type names so that this is no
    // longer necessary.
    static uint64_t id;
    string_name << "_" << id++;
    return string_name.str();
}

std::string RequestName(StringView name) {
    std::string request_name(name);
    request_name += "_Request";
    return request_name;
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

void TablesGenerator::GenerateForward(const coded::Type& type) {
    bool external = false;
    bool pointer = false;
    switch (type.kind) {
    case coded::Type::Kind::kStruct:
        external = true;
        pointer = true;
        break;
    case coded::Type::Kind::kUnion:
        pointer = true;
        break;
    default:
        break;
    }

    Emit(&tables_file_, external ? "extern" : "static");
    Emit(&tables_file_, " const fidl_type_t ");
    Emit(&tables_file_, TypeName(type.coded_name));
    Emit(&tables_file_, ";\n");

    if (pointer) {
        Emit(&tables_file_, "static const fidl_type_t ");
        Emit(&tables_file_, TypeName(PointerName(type.coded_name)));
        Emit(&tables_file_, ";\n");
    }
}

void TablesGenerator::Generate(const coded::StructType& struct_type) {
    Emit(&tables_file_, "static const fidl::FidlField ");
    Emit(&tables_file_, FieldsName(struct_type.coded_name));
    Emit(&tables_file_, "[] = ");
    GenerateArray(struct_type.fields);
    Emit(&tables_file_, ";\n");

    Emit(&tables_file_, "const fidl_type_t ");
    Emit(&tables_file_, TypeName(struct_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(fidl::FidlCodedStruct(");
    Emit(&tables_file_, FieldsName(struct_type.coded_name));
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, struct_type.fields.size());
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, struct_type.size);
    Emit(&tables_file_, "));\n\n");

    Emit(&tables_file_, "static const fidl_type_t ");
    Emit(&tables_file_, TypeName(PointerName(struct_type.coded_name)));
    Emit(&tables_file_, " = fidl_type_t(fidl::FidlCodedStructPointer(&");
    Emit(&tables_file_, TypeName(struct_type.coded_name));
    Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::UnionType& union_type) {
    Emit(&tables_file_, "static const fidl::FidlField ");
    Emit(&tables_file_, TypeName(MembersName(union_type.coded_name)));
    Emit(&tables_file_, "[] = ");
    GenerateArray(union_type.types);
    Emit(&tables_file_, ";\n");

    Emit(&tables_file_, "const fidl_type_t ");
    Emit(&tables_file_, TypeName(union_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(fidl::FidlCodedUnion(");
    Emit(&tables_file_, MembersName(union_type.coded_name));
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, union_type.types.size());
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, union_type.size);
    Emit(&tables_file_, "));\n\n");

    Emit(&tables_file_, "static const fidl_type_t ");
    Emit(&tables_file_, TypeName(PointerName(union_type.coded_name)));
    Emit(&tables_file_, " = fidl_type_t(fidl::FidlCodedUnionPointer(&");
    Emit(&tables_file_, TypeName(union_type.coded_name));
    Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::HandleType& handle_type) {
    Emit(&tables_file_, "static const fidl_type_t ");
    Emit(&tables_file_, TypeName(handle_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(fidl::FidlCodedHandle(");
    Emit(&tables_file_, handle_type.subtype);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, handle_type.nullability);
    Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::ArrayType& array_type) {
    Emit(&tables_file_, "static const fidl_type_t ");
    Emit(&tables_file_, TypeName(array_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(fidl::FidlCodedArray(&");
    Emit(&tables_file_, array_type.element_type->coded_name);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, array_type.array_size);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, array_type.element_size);
    Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::StringType& string_type) {
    Emit(&tables_file_, "static const fidl_type_t ");
    Emit(&tables_file_, TypeName(string_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(FidlCodedString(");
    Emit(&tables_file_, string_type.max_size);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, string_type.nullability);
    Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::VectorType& vector_type) {
    Emit(&tables_file_, "static const fidl_type_t ");
    Emit(&tables_file_, TypeName(vector_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(fidl::FidlCodedVector(");
    if (vector_type.element_type) {
        Emit(&tables_file_, "&");
        Emit(&tables_file_, vector_type.element_type->coded_name);
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
    Emit(&tables_file_, type->coded_name);
}

void TablesGenerator::Generate(const coded::Field& field) {
    Emit(&tables_file_, "fidl::FidlField(&");
    Emit(&tables_file_, field.type->coded_name);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, field.offset);
    Emit(&tables_file_, ")");
}

const coded::Type* TablesGenerator::LookupType(const flat::Type* type, StringView name) {
    switch (type->kind) {
    // TODO(TO-856) Store these so we can deduplicate them.
    case flat::Type::Kind::Handle:
    case flat::Type::Kind::Array:
    case flat::Type::Kind::Vector:
    case flat::Type::Kind::String:
        return Compile(type, name);

    case flat::Type::Kind::Primitive:
        return nullptr;

    case flat::Type::Kind::Request: {
        auto request_type = static_cast<const flat::RequestType*>(type);
        auto iter = named_type_map_.find(&request_type->name);
        if (iter == named_type_map_.end())
            return nullptr;
        return iter->second;
    }

    case flat::Type::Kind::Identifier: {
        auto identifier_type = static_cast<const flat::IdentifierType*>(type);
        auto iter = named_type_map_.find(&identifier_type->name);
        if (iter == named_type_map_.end())
            return nullptr;
        return iter->second;
    }
    }
}

const coded::Type* TablesGenerator::Compile(const flat::Type* type, StringView name) {
    switch (type->kind) {
    case flat::Type::Kind::Array: {
        auto array_type = static_cast<const flat::ArrayType*>(type);
        auto coded_element_type = LookupType(array_type->element_type.get(), name);
        // If the coded element type is absent, the type is
        // boring. Decoding this array is just memcpy, so don't emit
        // anything.
        if (coded_element_type != nullptr) {
            uint32_t array_size = array_type->size;
            uint32_t element_size = array_type->element_type->size;
            coded_array_types_.push_back(std::make_unique<coded::ArrayType>(ArrayName(name), coded_element_type, array_size, element_size));
        }
        return nullptr;
    }
    case flat::Type::Kind::Vector: {
        auto vector_type = static_cast<const flat::VectorType*>(type);
        auto coded_element_type = LookupType(vector_type->element_type.get(), name);
        // If the coded element type is absent, the type is
        // semi-boring: we still need to emit the size
        // information. Decoding the body of the vector is just
        // memcpy.
        uint32_t max_count = 0u;
        uint32_t element_size = 0u;
        if (coded_element_type) {
            max_count = vector_type->element_count.Value();
            element_size = vector_type->element_type->size;
        }
        coded_vector_types_.push_back(std::make_unique<coded::VectorType>(VectorName(name), coded_element_type, max_count, element_size, vector_type->nullability));
        auto coded_vector_type = coded_vector_types_.back().get();
        return coded_vector_type;
    }
    case flat::Type::Kind::String: {
        auto string_type = static_cast<const flat::StringType*>(type);
        uint32_t max_size = string_type->max_size.Value();
        coded_string_types_.push_back(std::make_unique<coded::StringType>(StringName(name), max_size, string_type->nullability));
        auto coded_string_type = coded_string_types_.back().get();
        return coded_string_type;
    }
    case flat::Type::Kind::Handle: {
        auto handle_type = static_cast<const flat::HandleType*>(type);
        auto handle_name = HandleName(name, handle_type->subtype, handle_type->nullability);
        coded_handle_types_.push_back(std::make_unique<coded::HandleType>(handle_name, handle_type->subtype, handle_type->nullability));
        auto coded_handle_type = coded_handle_types_.back().get();
        return coded_handle_type;
    }
    case flat::Type::Kind::Request: {
        auto request_type = static_cast<const flat::RequestType*>(type);
        coded_request_types_.push_back(std::make_unique<coded::HandleType>(RequestName(name), types::HandleSubtype::Channel, request_type->nullability));
        auto coded_request_type = coded_request_types_.back().get();
        named_type_map_[&request_type->name] = coded_request_type;
        return coded_request_type;
    }
    case flat::Type::Kind::Primitive: {
        return nullptr;
    }
    case flat::Type::Kind::Identifier: {
        // All identifier types are handled by the top level
        // flat::Decl compilation.
        return nullptr;
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
                std::string request_name = method_name + "_Request";
                for (const auto& parameter : method.maybe_request->parameters) {
                    std::string parameter_name = request_name + "_" + std::string(parameter.name.data());
                    Compile(parameter.type.get(), parameter_name);
                    auto coded_parameter_type = LookupType(parameter.type.get(), parameter_name);
                    if (coded_parameter_type)
                        request_fields.emplace_back(coded_parameter_type, parameter.fieldshape.Offset());
                }
                coded_struct_types_.push_back(std::make_unique<coded::StructType>(std::move(request_name), std::move(request_fields), method.maybe_request->typeshape.Size()));
                named_type_map_[&decl->name] = coded_struct_types_.back().get();
            }
            if (method.maybe_response) {
                std::string response_name = method_name + (method.maybe_request ? "_Response" : "_Event");
                std::vector<coded::Field> response_fields;
                for (const auto& parameter : method.maybe_response->parameters) {
                    std::string parameter_name = response_name + "_" + std::string(parameter.name.data());
                    Compile(parameter.type.get(), parameter_name);
                    auto coded_parameter_type = LookupType(parameter.type.get(), parameter_name);
                    if (coded_parameter_type)
                        response_fields.emplace_back(coded_parameter_type, parameter.fieldshape.Offset());
                }
                coded_struct_types_.push_back(std::make_unique<coded::StructType>(std::move(response_name), std::move(response_fields), method.maybe_response->typeshape.Size()));
                named_type_map_[&decl->name] = coded_struct_types_.back().get();
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
            Compile(member.type.get(), member_name);
            auto coded_member_type = LookupType(member.type.get(), member_name);
            if (coded_member_type)
                struct_fields.emplace_back(coded_member_type, member.fieldshape.Offset());
        }
        coded_struct_types_.push_back(std::make_unique<coded::StructType>(std::move(struct_name), std::move(struct_fields), struct_decl->typeshape.Size()));
        named_type_map_[&decl->name] = coded_struct_types_.back().get();
        break;
    }
    case flat::Decl::Kind::kUnion: {
        auto union_decl = static_cast<const flat::Union*>(decl);
        std::string union_name = LongName(union_decl->name);
        std::vector<const coded::Type*> union_members;
        for (const auto& member : union_decl->members) {
            std::string member_name = union_name + "_" + std::string(member.name.data());
            Compile(member.type.get(), member_name);
            auto coded_member_type = LookupType(member.type.get(), member_name);
            if (coded_member_type)
                union_members.push_back(coded_member_type);
        }
        coded_union_types_.push_back(std::make_unique<coded::UnionType>(std::move(union_name), std::move(union_members), union_decl->typeshape.Size()));
        named_type_map_[&decl->name] = coded_union_types_.back().get();
        break;
    }
    }
}

std::ostringstream TablesGenerator::Produce() {
    GenerateFilePreamble();

    for (const auto& decl : library_->declaration_order_)
        Compile(decl);

    EmitNewline(&tables_file_);
    for (const auto& coded_struct_type : coded_struct_types_)
        GenerateForward(*coded_struct_type);
    EmitNewline(&tables_file_);
    for (const auto& coded_union_type : coded_union_types_)
        GenerateForward(*coded_union_type);
    EmitNewline(&tables_file_);
    for (const auto& coded_handle_type : coded_handle_types_)
        GenerateForward(*coded_handle_type);
    EmitNewline(&tables_file_);
    for (const auto& coded_request_type : coded_request_types_)
        GenerateForward(*coded_request_type);
    EmitNewline(&tables_file_);
    for (const auto& coded_array_type : coded_array_types_)
        GenerateForward(*coded_array_type);
    EmitNewline(&tables_file_);
    for (const auto& coded_string_type : coded_string_types_)
        GenerateForward(*coded_string_type);
    EmitNewline(&tables_file_);
    for (const auto& coded_vector_type : coded_vector_types_)
        GenerateForward(*coded_vector_type);

    EmitNewline(&tables_file_);
    for (const auto& coded_struct_type : coded_struct_types_)
        Generate(*coded_struct_type);
    EmitNewline(&tables_file_);
    for (const auto& coded_union_type : coded_union_types_)
        Generate(*coded_union_type);
    EmitNewline(&tables_file_);
    for (const auto& coded_handle_type : coded_handle_types_)
        Generate(*coded_handle_type);
    EmitNewline(&tables_file_);
    for (const auto& coded_request_type : coded_request_types_)
        Generate(*coded_request_type);
    EmitNewline(&tables_file_);
    for (const auto& coded_array_type : coded_array_types_)
        Generate(*coded_array_type);
    EmitNewline(&tables_file_);
    for (const auto& coded_string_type : coded_string_types_)
        Generate(*coded_string_type);
    EmitNewline(&tables_file_);
    for (const auto& coded_vector_type : coded_vector_types_)
        Generate(*coded_vector_type);

    GenerateFilePostamble();

    return std::move(tables_file_);
}

} // namespace fidl
