// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat_ast.h"

#include <assert.h>
#include <stdio.h>

#include <algorithm>
#include <sstream>

#include "fidl/lexer.h"
#include "fidl/parser.h"
#include "fidl/raw_ast.h"

namespace fidl {
namespace flat {

namespace {

template <typename T>
class Scope {
public:
    bool Insert(const T& t) {
        auto iter = scope_.insert(t);
        return iter.second;
    }

private:
    std::set<T> scope_;
};

constexpr TypeShape kHandleTypeShape = TypeShape(4u, 4u);
constexpr TypeShape kInt8TypeShape = TypeShape(1u, 1u);
constexpr TypeShape kInt16TypeShape = TypeShape(2u, 2u);
constexpr TypeShape kInt32TypeShape = TypeShape(4u, 4u);
constexpr TypeShape kInt64TypeShape = TypeShape(8u, 8u);
constexpr TypeShape kUint8TypeShape = TypeShape(1u, 1u);
constexpr TypeShape kUint16TypeShape = TypeShape(2u, 2u);
constexpr TypeShape kUint32TypeShape = TypeShape(4u, 4u);
constexpr TypeShape kUint64TypeShape = TypeShape(8u, 8u);
constexpr TypeShape kBoolTypeShape = TypeShape(1u, 1u);
constexpr TypeShape kStatusTypeShape = TypeShape(4u, 4u);
constexpr TypeShape kFloat32TypeShape = TypeShape(4u, 4u);
constexpr TypeShape kFloat64TypeShape = TypeShape(8u, 8u);
constexpr TypeShape kPointerTypeShape = TypeShape(8u, 8u);

size_t AlignTo(size_t size, size_t alignment) {
    auto mask = alignment - 1;
    size += mask;
    size &= ~mask;
    return size;
}

TypeShape CStructTypeShape(std::vector<TypeShape> member_typeshapes) {
    size_t size = 0u;
    size_t alignment = 1u;

    for (const auto& type_shape : member_typeshapes) {
        alignment = std::max(alignment, type_shape.Alignment());
        size = AlignTo(size, type_shape.Alignment());
        size += type_shape.Size();
    }

    return TypeShape(size, alignment);
}

TypeShape FidlStructTypeShape(std::vector<TypeShape> member_typeshapes) {
    // TODO(kulakowski) Fit-sort members.
    return CStructTypeShape(std::move(member_typeshapes));
}

TypeShape CUnionTypeShape(std::vector<TypeShape> member_typeshapes) {
    size_t size = 0u;
    size_t alignment = 1u;
    for (const auto& type_shape : member_typeshapes) {
        size = std::max(size, type_shape.Size());
        alignment = std::max(alignment, type_shape.Alignment());
    }
    size = AlignTo(size, alignment);
    return TypeShape(size, alignment);
}

TypeShape FidlUnionTypeShape(std::vector<TypeShape> member_typeshapes) {
    std::vector<TypeShape> fidl_union;
    fidl_union.push_back(kUint32TypeShape);
    fidl_union.push_back(CUnionTypeShape(std::move(member_typeshapes)));
    return CStructTypeShape(std::move(fidl_union));
}

TypeShape ArrayTypeShape(TypeShape element, uint64_t count) {
    return TypeShape(element.Size() * count, element.Alignment());
}

TypeShape VectorTypeShape(TypeShape element, uint64_t count) {
    auto header_shape =
        CStructTypeShape(std::vector<TypeShape>({kUint64TypeShape, kPointerTypeShape}));
    return header_shape;
}

TypeShape StringTypeShape(uint64_t count) {
    auto header_shape =
        CStructTypeShape(std::vector<TypeShape>({kUint64TypeShape, kPointerTypeShape}));
    return header_shape;
}

} // namespace

// Consuming the AST is primarily concerned with walking the tree and
// flattening the representation. The AST's declaration nodes are
// converted into the Library's foo_declaration structures. This means pulling
// a struct declaration inside an interface out to the top level and
// so on.

bool Library::ConsumeConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const_declaration) {
    auto name = Name(std::move(const_declaration->identifier));

    if (!RegisterTypeName(name))
        return false;
    const_declarations_.emplace_back(std::move(name), std::move(const_declaration->type),
                                     std::move(const_declaration->constant));
    return true;
}

bool Library::ConsumeEnumDeclaration(std::unique_ptr<raw::EnumDeclaration> enum_declaration) {
    std::vector<Enum::Member> members;
    for (auto& member : enum_declaration->members) {
        auto name = Name(std::move(member->identifier));
        auto value = std::move(member->value);
        members.emplace_back(std::move(name), std::move(value));
    }
    std::unique_ptr<raw::PrimitiveType> type = std::move(enum_declaration->maybe_subtype);
    if (!type)
        type = std::make_unique<raw::PrimitiveType>(types::PrimitiveSubtype::Uint32);
    auto name = Name(std::move(enum_declaration->identifier));

    if (!RegisterTypeName(name))
        return false;
    enum_declarations_.emplace_back(std::move(name), std::move(type), std::move(members));
    return true;
}

bool Library::ConsumeInterfaceDeclaration(
    std::unique_ptr<raw::InterfaceDeclaration> interface_declaration) {
    auto name = Name(std::move(interface_declaration->identifier));

    for (auto& const_member : interface_declaration->const_members)
        if (!ConsumeConstDeclaration(std::move(const_member)))
            return false;
    for (auto& enum_member : interface_declaration->enum_members)
        if (!ConsumeEnumDeclaration(std::move(enum_member)))
            return false;

    std::vector<Interface::Method> methods;
    for (auto& method : interface_declaration->method_members) {
        auto ordinal_literal = std::move(method->ordinal);
        uint32_t value;
        if (!ParseIntegerLiteral<decltype(value)>(ordinal_literal.get(), &value))
            return false;
        if (value == 0u)
            return false;
        Ordinal ordinal(std::move(ordinal_literal), value);

        auto method_name = std::move(method->identifier);

        bool has_request = static_cast<bool>(method->maybe_request);
        std::vector<Interface::Method::Parameter> maybe_request;
        if (has_request) {
            for (auto& parameter : method->maybe_request->parameter_list) {
                auto parameter_name = std::move(parameter->identifier);
                maybe_request.emplace_back(std::move(parameter->type), std::move(parameter_name));
            }
        }

        bool has_response = static_cast<bool>(method->maybe_response);
        std::vector<Interface::Method::Parameter> maybe_response;
        if (has_response) {
            for (auto& parameter : method->maybe_response->parameter_list) {
                auto response_paramater_name = std::move(parameter->identifier);
                maybe_response.emplace_back(std::move(parameter->type),
                                            std::move(response_paramater_name));
            }
        }

        assert(has_request || has_response);

        methods.emplace_back(std::move(ordinal), std::move(method_name),
                             has_request, std::move(maybe_request),
                             has_response, std::move(maybe_response));
    }

    if (!RegisterTypeName(name))
        return false;
    interface_declarations_.emplace_back(std::move(name), std::move(methods));
    return true;
}

bool Library::ConsumeStructDeclaration(std::unique_ptr<raw::StructDeclaration> struct_declaration) {
    auto name = Name(std::move(struct_declaration->identifier));

    for (auto& const_member : struct_declaration->const_members)
        if (!ConsumeConstDeclaration(std::move(const_member)))
            return false;
    for (auto& enum_member : struct_declaration->enum_members)
        if (!ConsumeEnumDeclaration(std::move(enum_member)))
            return false;

    std::vector<Struct::Member> members;
    for (auto& member : struct_declaration->members) {
        auto name = std::move(member->identifier);
        members.emplace_back(std::move(member->type), std::move(name),
                             std::move(member->maybe_default_value));
    }

    if (!RegisterTypeName(name))
        return false;
    struct_declarations_.emplace_back(std::move(name), std::move(members));
    return true;
}

bool Library::ConsumeUnionDeclaration(std::unique_ptr<raw::UnionDeclaration> union_declaration) {
    std::vector<Union::Member> members;
    for (auto& member : union_declaration->members) {
        auto name = std::move(member->identifier);
        members.emplace_back(std::move(member->type), std::move(name));
    }
    auto name = Name(std::move(union_declaration->identifier));

    if (!RegisterTypeName(name))
        return false;
    union_declarations_.emplace_back(std::move(name), std::move(members));
    return true;
}

bool Library::ConsumeFile(std::unique_ptr<raw::File> file) {
    // All fidl files in a library should agree on the library name.
    if (file->identifier->components.size() != 1) {
        return false;
    }
    auto library_name = std::move(file->identifier->components[0]);

    if (library_name_ == nullptr) {
        library_name_ = std::move(library_name);
    } else {
        StringView current_name = library_name_->location.data();
        StringView new_name = library_name->location.data();
        if (current_name != new_name) {
            return false;
        }
    }

    auto using_list = std::move(file->using_list);

    auto const_declaration_list = std::move(file->const_declaration_list);
    for (auto& const_declaration : const_declaration_list) {
        if (!ConsumeConstDeclaration(std::move(const_declaration))) {
            return false;
        }
    }

    auto enum_declaration_list = std::move(file->enum_declaration_list);
    for (auto& enum_declaration : enum_declaration_list) {
        if (!ConsumeEnumDeclaration(std::move(enum_declaration))) {
            return false;
        }
    }

    auto interface_declaration_list = std::move(file->interface_declaration_list);
    for (auto& interface_declaration : interface_declaration_list) {
        if (!ConsumeInterfaceDeclaration(std::move(interface_declaration))) {
            return false;
        }
    }

    auto struct_declaration_list = std::move(file->struct_declaration_list);
    for (auto& struct_declaration : struct_declaration_list) {
        if (!ConsumeStructDeclaration(std::move(struct_declaration))) {
            return false;
        }
    }

    auto union_declaration_list = std::move(file->union_declaration_list);
    for (auto& union_declaration : union_declaration_list) {
        if (!ConsumeUnionDeclaration(std::move(union_declaration))) {
            return false;
        }
    }

    return true;
}

bool Library::RegisterTypeName(const Name& name) {
    // TODO(TO-701) Should this copy the Name?
    // auto iter = registered_types_.insert(name);
    // return iter.second;
    return true;
}

bool Library::RegisterResolvedType(const Name& name, TypeShape typeshape) {
    // TODO(TO-701) Should this copy the Name?
    // auto key_value = std::make_pair(name, typeshape);
    // auto iter = resolved_types_.insert(std::move(key_value));
    // return iter.second;
    return true;
}

bool Library::LookupTypeShape(const Name& name, TypeShape* out_typeshape) {
    auto iter = resolved_types_.find(name);
    if (iter == resolved_types_.end()) {
        return false;
    }
    *out_typeshape = iter->second;
    return true;
}

// Library resolution is concerned with resolving identifiers to their
// declarations, and with computing type sizes and alignments.

bool Library::ResolveConst(const Const& const_declaration) {
    if (!ResolveType(const_declaration.type.get())) {
        return false;
    }
    // TODO(TO-702) Resolve const declarations.
    return true;
}

bool Library::ResolveEnum(const Enum& enum_declaration) {
    TypeShape typeshape;

    switch (enum_declaration.type->subtype) {
    case types::PrimitiveSubtype::Int8:
    case types::PrimitiveSubtype::Int16:
    case types::PrimitiveSubtype::Int32:
    case types::PrimitiveSubtype::Int64:
    case types::PrimitiveSubtype::Uint8:
    case types::PrimitiveSubtype::Uint16:
    case types::PrimitiveSubtype::Uint32:
    case types::PrimitiveSubtype::Uint64:
        // These are allowed as enum subtypes. Resolve the size and alignment.
        if (!ResolveType(enum_declaration.type.get(), &typeshape))
            return false;
        break;

    case types::PrimitiveSubtype::Bool:
    case types::PrimitiveSubtype::Status:
    case types::PrimitiveSubtype::Float32:
    case types::PrimitiveSubtype::Float64:
        // These are not allowed as enum subtypes.
        return false;
    }

    if (!RegisterResolvedType(enum_declaration.name, typeshape)) {
        return false;
    }

    // TODO(TO-702) Validate values.
    return true;
}

bool Library::ResolveInterface(const Interface& interface_declaration) {
    // TODO(TO-703) Add subinterfaces here.
    Scope<StringView> name_scope;
    Scope<uint32_t> ordinal_scope;
    for (const auto& method : interface_declaration.methods) {
        if (!name_scope.Insert(method.name->location.data()))
            return false;
        if (!ordinal_scope.Insert(method.ordinal.Value()))
            return false;
        if (method.has_request) {
            Scope<StringView> request_scope;
            for (const auto& param : method.maybe_request) {
                if (!request_scope.Insert(param.name->location.data()))
                    return false;
                if (!ResolveType(param.type.get()))
                    return false;
            }
        }
        if (method.has_response) {
            Scope<StringView> response_scope;
            for (const auto& response_param : method.maybe_response) {
                if (!response_scope.Insert(response_param.name->location.data()))
                    return false;
                if (!ResolveType(response_param.type.get()))
                    return false;
            }
        }
    }
    return true;
}

bool Library::ResolveStruct(const Struct& struct_declaration) {
    Scope<StringView> scope;
    std::vector<TypeShape> member_typeshapes;
    for (const auto& member : struct_declaration.members) {
        if (!scope.Insert(member.name->location.data()))
            return false;
        TypeShape member_typeshape;
        if (!ResolveType(member.type.get(), &member_typeshape))
            return false;
        member_typeshapes.push_back(member_typeshape);
    }

    auto type_shape = FidlStructTypeShape(std::move(member_typeshapes));
    if (!RegisterResolvedType(struct_declaration.name, type_shape))
        return false;

    return true;
}

bool Library::ResolveUnion(const Union& union_declaration) {
    Scope<StringView> scope;
    std::vector<TypeShape> member_typeshapes;
    for (const auto& member : union_declaration.members) {
        if (!scope.Insert(member.name->location.data()))
            return false;
        TypeShape member_typeshape;
        if (!ResolveType(member.type.get(), &member_typeshape))
            return false;
        member_typeshapes.push_back(member_typeshape);
    }

    auto typeshape = FidlUnionTypeShape(std::move(member_typeshapes));
    if (!RegisterResolvedType(union_declaration.name, typeshape))
        return false;

    return true;
}

bool Library::Resolve() {
    for (const auto& const_declaration : const_declarations_) {
        if (!ResolveConst(const_declaration)) {
            return false;
        }
    }

    for (const auto& enum_declaration : enum_declarations_) {
        if (!ResolveEnum(enum_declaration)) {
            return false;
        }
    }

    for (const auto& interface_declaration : interface_declarations_) {
        if (!ResolveInterface(interface_declaration)) {
            return false;
        }
    }

    for (const auto& struct_declaration : struct_declarations_) {
        if (!ResolveStruct(struct_declaration)) {
            return false;
        }
    }

    for (const auto& union_declaration : union_declarations_) {
        if (!ResolveUnion(union_declaration)) {
            return false;
        }
    }

    return true;
}

bool Library::ResolveArrayType(const raw::ArrayType& array_type, TypeShape* out_typeshape) {
    TypeShape element_typeshape;
    if (!ResolveType(array_type.element_type.get(), &element_typeshape))
        return false;
    uint64_t element_count;
    if (!ParseIntegerConstant<decltype(element_count)>(array_type.element_count.get(),
                                                       &element_count))
        return false;
    if (element_count == 0) {
        return false;
    }
    *out_typeshape = ArrayTypeShape(element_typeshape, element_count);
    return true;
}

bool Library::ResolveVectorType(const raw::VectorType& vector_type, TypeShape* out_typeshape) {
    TypeShape element_typeshape;
    if (!ResolveType(vector_type.element_type.get(), &element_typeshape)) {
        return false;
    }
    auto element_count = std::numeric_limits<uint64_t>::max();
    if (vector_type.maybe_element_count) {
        if (!ParseIntegerConstant(vector_type.maybe_element_count.get(), &element_count)) {
            return false;
        }
        if (element_count == 0u) {
            return false;
        }
    }
    *out_typeshape = VectorTypeShape(element_typeshape, element_count);
    return true;
}

bool Library::ResolveStringType(const raw::StringType& string_type, TypeShape* out_typeshape) {
    auto byte_count = std::numeric_limits<uint64_t>::max();
    if (string_type.maybe_element_count) {
        if (!ParseIntegerConstant(string_type.maybe_element_count.get(), &byte_count)) {
            return false;
        }
        if (byte_count == 0u) {
            return false;
        }
    }
    *out_typeshape = StringTypeShape(byte_count);
    return true;
}

bool Library::ResolveHandleType(const raw::HandleType& handle_type, TypeShape* out_typeshape) {
    // Nothing to check.
    *out_typeshape = kHandleTypeShape;
    return true;
}

bool Library::ResolveRequestType(const raw::RequestType& request_type, TypeShape* out_typeshape) {
    if (!ResolveTypeName(request_type.subtype.get())) {
        return false;
    }
    *out_typeshape = kHandleTypeShape;
    return true;
}

bool Library::ResolvePrimitiveType(const raw::PrimitiveType& primitive_type,
                                  TypeShape* out_typeshape) {
    switch (primitive_type.subtype) {
    case types::PrimitiveSubtype::Int8:
        *out_typeshape = kInt8TypeShape;
        break;
    case types::PrimitiveSubtype::Int16:
        *out_typeshape = kInt16TypeShape;
        break;
    case types::PrimitiveSubtype::Int32:
        *out_typeshape = kInt32TypeShape;
        break;
    case types::PrimitiveSubtype::Int64:
        *out_typeshape = kInt64TypeShape;
        break;
    case types::PrimitiveSubtype::Uint8:
        *out_typeshape = kUint8TypeShape;
        break;
    case types::PrimitiveSubtype::Uint16:
        *out_typeshape = kUint16TypeShape;
        break;
    case types::PrimitiveSubtype::Uint32:
        *out_typeshape = kUint32TypeShape;
        break;
    case types::PrimitiveSubtype::Uint64:
        *out_typeshape = kUint64TypeShape;
        break;
    case types::PrimitiveSubtype::Bool:
        *out_typeshape = kBoolTypeShape;
        break;
    case types::PrimitiveSubtype::Status:
        *out_typeshape = kStatusTypeShape;
        break;
    case types::PrimitiveSubtype::Float32:
        *out_typeshape = kFloat32TypeShape;
        break;
    case types::PrimitiveSubtype::Float64:
        *out_typeshape = kFloat64TypeShape;
        break;
    }
    return true;
}

bool Library::ResolveIdentifierType(const raw::IdentifierType& identifier_type,
                                   TypeShape* out_typeshape) {
    if (!ResolveTypeName(identifier_type.identifier.get()))
        return false;
    // TODO(TO-702) identifier type shape
    *out_typeshape = TypeShape(184u, 8u);
    return true;
}

bool Library::ResolveType(const raw::Type* type, TypeShape* out_typeshape) {
    switch (type->kind) {
    case raw::Type::Kind::Array: {
        auto array_type = static_cast<const raw::ArrayType*>(type);
        return ResolveArrayType(*array_type, out_typeshape);
    }

    case raw::Type::Kind::Vector: {
        auto vector_type = static_cast<const raw::VectorType*>(type);
        return ResolveVectorType(*vector_type, out_typeshape);
    }

    case raw::Type::Kind::String: {
        auto string_type = static_cast<const raw::StringType*>(type);
        return ResolveStringType(*string_type, out_typeshape);
    }

    case raw::Type::Kind::Handle: {
        auto handle_type = static_cast<const raw::HandleType*>(type);
        return ResolveHandleType(*handle_type, out_typeshape);
    }

    case raw::Type::Kind::Request: {
        auto request_type = static_cast<const raw::RequestType*>(type);
        return ResolveRequestType(*request_type, out_typeshape);
    }

    case raw::Type::Kind::Primitive: {
        auto primitive_type = static_cast<const raw::PrimitiveType*>(type);
        return ResolvePrimitiveType(*primitive_type, out_typeshape);
    }

    case raw::Type::Kind::Identifier: {
        auto identifier_type = static_cast<const raw::IdentifierType*>(type);
        return ResolveIdentifierType(*identifier_type, out_typeshape);
    }
    }
}

bool Library::ResolveTypeName(const raw::CompoundIdentifier* name) {
    // TODO(TO-701) Make this use Names.
    // assert(name->components.size() == 1);
    // StringView identifier = name->components[0]->identifier.data();
    // return registered_types_.find(identifier) != registered_types_.end();
    return true;
}

} // namespace flat
} // namespace fidl
