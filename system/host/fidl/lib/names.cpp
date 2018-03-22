// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/names.h"

namespace fidl {

namespace {

const char* NameNullability(types::Nullability nullability) {
    switch (nullability) {
    case types::Nullability::Nonnullable:
        return "nonnullable";
    case types::Nullability::Nullable:
        return "nullable";
    }
}

std::string NameSize(uint64_t size) {
    if (size == std::numeric_limits<uint64_t>::max())
        return "unbounded";
    std::ostringstream name;
    name << size;
    return name.str();
}

} // namespace

std::string NamePrimitiveCType(types::PrimitiveSubtype subtype) {
    switch (subtype) {
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

std::string NamePrimitiveSubtype(types::PrimitiveSubtype subtype) {
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

std::string NamePrimitiveIntegerCConstantMacro(types::PrimitiveSubtype subtype) {
    switch (subtype) {
    case types::PrimitiveSubtype::Int8:
        return "INT8_C";
    case types::PrimitiveSubtype::Int16:
        return "INT16_C";
    case types::PrimitiveSubtype::Int32:
    case types::PrimitiveSubtype::Status:
        return "INT32_C";
    case types::PrimitiveSubtype::Int64:
        return "INT64_C";
    case types::PrimitiveSubtype::Uint8:
        return "UINT8_C";
    case types::PrimitiveSubtype::Uint16:
        return "UINT16_C";
    case types::PrimitiveSubtype::Uint32:
        return "UINT32_C";
    case types::PrimitiveSubtype::Uint64:
        return "UINT64_C";
    case types::PrimitiveSubtype::Bool:
        assert(false && "Tried to generate an integer constant for a bool");
        return "";
    case types::PrimitiveSubtype::Float32:
    case types::PrimitiveSubtype::Float64:
        assert(false && "Tried to generate an integer constant for a float");
        return "";
    }
}

std::string NameHandleSubtype(types::HandleSubtype subtype) {
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

std::string NameRawLiteralKind(raw::Literal::Kind kind) {
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

std::string NameFlatTypeKind(flat::Type::Kind kind) {
    switch (kind) {
    case flat::Type::Kind::Array:
        return "array";
    case flat::Type::Kind::Vector:
        return "vector";
    case flat::Type::Kind::String:
        return "string";
    case flat::Type::Kind::Handle:
        return "handle";
    case flat::Type::Kind::RequestHandle:
        return "request";
    case flat::Type::Kind::Primitive:
        return "primitive";
    case flat::Type::Kind::Identifier:
        return "identifier";
    }
}

std::string NameRawConstantKind(raw::Constant::Kind kind) {
    switch (kind) {
    case raw::Constant::Kind::Identifier:
        return "identifier";
    case raw::Constant::Kind::Literal:
        return "literal";
    }
}

std::string NameHandleZXObjType(types::HandleSubtype subtype) {
    switch (subtype) {
    case types::HandleSubtype::Handle:
        return "ZX_OBJ_TYPE_NONE";
    case types::HandleSubtype::Process:
        return "ZX_OBJ_TYPE_PROCESS";
    case types::HandleSubtype::Thread:
        return "ZX_OBJ_TYPE_THREAD";
    case types::HandleSubtype::Vmo:
        return "ZX_OBJ_TYPE_VMO";
    case types::HandleSubtype::Channel:
        return "ZX_OBJ_TYPE_CHANNEL";
    case types::HandleSubtype::Event:
        return "ZX_OBJ_TYPE_EVENT";
    case types::HandleSubtype::Port:
        return "ZX_OBJ_TYPE_PORT";
    case types::HandleSubtype::Interrupt:
        return "ZX_OBJ_TYPE_INTERRUPT";
    case types::HandleSubtype::Log:
        return "ZX_OBJ_TYPE_LOG";
    case types::HandleSubtype::Socket:
        return "ZX_OBJ_TYPE_SOCKET";
    case types::HandleSubtype::Resource:
        return "ZX_OBJ_TYPE_RESOURCE";
    case types::HandleSubtype::Eventpair:
        return "ZX_OBJ_TYPE_EVENT_PAIR";
    case types::HandleSubtype::Job:
        return "ZX_OBJ_TYPE_JOB";
    case types::HandleSubtype::Vmar:
        return "ZX_OBJ_TYPE_VMAR";
    case types::HandleSubtype::Fifo:
        return "ZX_OBJ_TYPE_FIFO";
    case types::HandleSubtype::Guest:
        return "ZX_OBJ_TYPE_GUEST";
    case types::HandleSubtype::Timer:
        return "ZX_OBJ_TYPE_TIMER";
    }
}

std::string NameUnionTag(StringView union_name, const flat::Union::Member& member) {
    return std::string(union_name) + "Tag" + NameIdentifier(member.name);
}

std::string NameFlatCType(const flat::Type* type) {
    for (;;) {
        switch (type->kind) {
        case flat::Type::Kind::Handle:
        case flat::Type::Kind::RequestHandle:
            return "zx_handle_t";

        case flat::Type::Kind::Vector:
            return "fidl_vector_t";
        case flat::Type::Kind::String:
            return "fidl_string_t";

        case flat::Type::Kind::Primitive: {
            auto primitive_type = static_cast<const flat::PrimitiveType*>(type);
            return NamePrimitiveCType(primitive_type->subtype);
        }

        case flat::Type::Kind::Array: {
            auto array_type = static_cast<const flat::ArrayType*>(type);
            type = array_type->element_type.get();
            continue;
        }

        case flat::Type::Kind::Identifier: {
            auto identifier_type = static_cast<const flat::IdentifierType*>(type);
            std::string name = identifier_type->name.name().data();
            if (identifier_type->nullability == types::Nullability::Nullable) {
                name.push_back('*');
            }
            return name;
        }
        }
    }
}

std::string NameIdentifier(SourceLocation name) {
    // TODO(TO-704) C name escaping and ergonomics.
    return name.data();
}

std::string NameName(const flat::Name& name) {
    // TODO(TO-701) Handle complex names.
    return name.name().data();
}

std::string NameInterface(const flat::Interface& interface) {
    return NameName(interface.name);
}

std::string NameMethod(StringView interface_name,
                       const flat::Interface::Method& method) {
    return std::string(interface_name) + NameIdentifier(method.name);
}

std::string NameOrdinal(StringView method_name) {
    std::string ordinal_name(method_name);
    ordinal_name += "Ordinal";
    return ordinal_name;
}

std::string NameMessage(StringView method_name, types::MessageKind kind) {
    std::string message_name(method_name);
    switch (kind) {
    case types::MessageKind::kRequest:
        message_name += "Request";
        break;
    case types::MessageKind::kResponse:
        message_name += "Response";
        break;
    case types::MessageKind::kEvent:
        message_name += "Event";
        break;
    }
    return message_name;
}

std::string NameTable(StringView type_name) {
    return std::string(type_name) + "Table";
}

std::string NamePointer(StringView name) {
    std::string pointer_name(name);
    pointer_name += "Pointer";
    return NameTable(pointer_name);
}

std::string NameMembers(StringView name) {
    std::string members_name(name);
    members_name += "Members";
    return members_name;
}

std::string NameFields(StringView name) {
    std::string fields_name(name);
    fields_name += "Fields";
    return fields_name;
}

std::string NameCodedHandle(types::HandleSubtype subtype, types::Nullability nullability) {
    std::string name("Handle");
    name += NameHandleSubtype(subtype);
    name += NameNullability(nullability);
    return name;
}

std::string NameCodedInterfaceHandle(StringView interface_name, types::Nullability nullability) {
    std::string name("Interface");
    name += interface_name;
    name += NameNullability(nullability);
    return name;
}

std::string NameCodedRequestHandle(StringView interface_name, types::Nullability nullability) {
    std::string name("Request");
    name += interface_name;
    name += NameNullability(nullability);
    return name;
}

std::string NameCodedArray(StringView element_name, uint64_t size) {
    std::string name("Array");
    name += element_name;
    name += NameSize(size);
    return name;
}

std::string NameCodedVector(StringView element_name, uint64_t max_size, types::Nullability nullability) {
    std::string name("Vector");
    name += element_name;
    name += NameSize(max_size);
    name += NameNullability(nullability);
    return name;
}

std::string NameCodedString(uint64_t max_size, types::Nullability nullability) {
    std::string name("String");
    name += NameSize(max_size);
    name += NameNullability(nullability);
    return name;
}

} // namespace fidl
