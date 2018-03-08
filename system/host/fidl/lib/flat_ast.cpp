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

uint64_t AlignTo(uint64_t size, uint64_t alignment) {
    auto mask = alignment - 1;
    size += mask;
    size &= ~mask;
    return size;
}

TypeShape CStructTypeShape(std::vector<FieldShape*>* fields) {
    uint64_t size = 0u;
    uint64_t alignment = 1u;

    for (FieldShape* field : *fields) {
        field->SetOffset(size);
        TypeShape typeshape = field->Typeshape();
        alignment = std::max(alignment, typeshape.Alignment());
        size = AlignTo(size, typeshape.Alignment());
        size += typeshape.Size();
    }

    return TypeShape(size, alignment);
}

TypeShape CUnionTypeShape(const std::vector<flat::Union::Member>& members) {
    uint64_t size = 0u;
    uint64_t alignment = 1u;
    for (const auto& member : members) {
        const auto& fieldshape = member.fieldshape;
        size = std::max(size, fieldshape.Size());
        alignment = std::max(alignment, fieldshape.Alignment());
    }
    size = AlignTo(size, alignment);
    return TypeShape(size, alignment);
}

TypeShape FidlStructTypeShape(std::vector<FieldShape*>* fields) {
    // TODO(kulakowski) Fit-sort members.
    return CStructTypeShape(fields);
}

TypeShape ArrayTypeShape(TypeShape element, uint64_t count) {
    return TypeShape(element.Size() * count, element.Alignment());
}

TypeShape VectorTypeShape() {
    auto size = FieldShape(kUint64TypeShape);
    auto data = FieldShape(kPointerTypeShape);
    std::vector<FieldShape*> header{&size, &data};
    return CStructTypeShape(&header);
}

TypeShape StringTypeShape() {
    auto size = FieldShape(kUint64TypeShape);
    auto data = FieldShape(kPointerTypeShape);
    std::vector<FieldShape*> header{&size, &data};
    return CStructTypeShape(&header);
}

TypeShape PrimitiveTypeShape(types::PrimitiveSubtype type) {
    switch (type) {
    case types::PrimitiveSubtype::Int8:
        return kInt8TypeShape;
    case types::PrimitiveSubtype::Int16:
        return kInt16TypeShape;
    case types::PrimitiveSubtype::Int32:
        return kInt32TypeShape;
    case types::PrimitiveSubtype::Int64:
        return kInt64TypeShape;
    case types::PrimitiveSubtype::Uint8:
        return kUint8TypeShape;
    case types::PrimitiveSubtype::Uint16:
        return kUint16TypeShape;
    case types::PrimitiveSubtype::Uint32:
        return kUint32TypeShape;
    case types::PrimitiveSubtype::Uint64:
        return kUint64TypeShape;
    case types::PrimitiveSubtype::Bool:
        return kBoolTypeShape;
    case types::PrimitiveSubtype::Status:
        return kStatusTypeShape;
    case types::PrimitiveSubtype::Float32:
        return kFloat32TypeShape;
    case types::PrimitiveSubtype::Float64:
        return kFloat64TypeShape;
    }
}

} // namespace

// Consuming the AST is primarily concerned with walking the tree and
// flattening the representation. The AST's declaration nodes are
// converted into the Library's foo_declaration structures. This means pulling
// a struct declaration inside an interface out to the top level and
// so on.

bool Library::ParseSize(std::unique_ptr<raw::Constant> raw_constant, Size* out_size) {
    uint64_t value;
    if (!ParseIntegerConstant(raw_constant.get(), &value)) {
        *out_size = Size();
        return false;
    }
    *out_size = Size(std::move(raw_constant), value);
    return true;
}

bool Library::RegisterDecl(Decl* decl) {
    const Name* name = &decl->name;
    auto iter = declarations_.emplace(name, decl);
    return iter.second;
}

bool Library::ConsumeType(std::unique_ptr<raw::Type> raw_type, std::unique_ptr<Type>* out_type) {
    switch (raw_type->kind) {
    case raw::Type::Kind::Array: {
        auto array_type = static_cast<raw::ArrayType*>(raw_type.get());
        std::unique_ptr<Type> element_type;
        if (!ConsumeType(std::move(array_type->element_type), &element_type))
            return false;
        Size element_count;
        if (!ParseSize(std::move(array_type->element_count), &element_count))
            return false;
        // TODO(kulakowski) Overflow checking.
        uint64_t size = element_count.Value() * element_type->size;
        *out_type = std::make_unique<ArrayType>(size, std::move(element_type), std::move(element_count));
        break;
    }
    case raw::Type::Kind::Vector: {
        auto vector_type = static_cast<raw::VectorType*>(raw_type.get());
        std::unique_ptr<Type> element_type;
        if (!ConsumeType(std::move(vector_type->element_type), &element_type))
            return false;
        Size element_count = Size::Max();
        if (vector_type->maybe_element_count) {
            if (!ParseSize(std::move(vector_type->maybe_element_count), &element_count))
                return false;
        }
        *out_type = std::make_unique<VectorType>(std::move(element_type), std::move(element_count), vector_type->nullability);
        break;
    }
    case raw::Type::Kind::String: {
        auto string_type = static_cast<raw::StringType*>(raw_type.get());
        Size element_count = Size::Max();
        if (string_type->maybe_element_count) {
            if (!ParseSize(std::move(string_type->maybe_element_count), &element_count))
                return false;
        }
        *out_type = std::make_unique<StringType>(std::move(element_count), string_type->nullability);
        break;
    }
    case raw::Type::Kind::Handle: {
        auto handle_type = static_cast<raw::HandleType*>(raw_type.get());
        *out_type = std::make_unique<HandleType>(handle_type->subtype, handle_type->nullability);
        break;
    }
    case raw::Type::Kind::Request: {
        auto request_type = static_cast<raw::RequestType*>(raw_type.get());
        // TODO(TO-701) Handle longer names.
        Name name(request_type->identifier->components[0]->location);
        *out_type = std::make_unique<RequestType>(std::move(name), std::move(request_type->nullability));
        break;
    }
    case raw::Type::Kind::Primitive: {
        auto primitive_type = static_cast<raw::PrimitiveType*>(raw_type.get());
        *out_type = std::make_unique<PrimitiveType>(primitive_type->subtype);
        break;
    }
    case raw::Type::Kind::Identifier: {
        auto identifier_type = static_cast<raw::IdentifierType*>(raw_type.get());
        // TODO(TO-701) Handle longer names.
        Name name(identifier_type->identifier->components[0]->location);
        *out_type = std::make_unique<IdentifierType>(std::move(name), identifier_type->nullability);
        break;
    }
    }
    return true;
}

bool Library::ConsumeConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const_declaration) {
    auto name = Name(const_declaration->identifier->location);
    std::unique_ptr<Type> type;
    if (!ConsumeType(std::move(const_declaration->type), &type))
        return false;

    const_declarations_.push_back(std::make_unique<Const>(std::move(name), std::move(type),
                                                          std::move(const_declaration->constant)));
    return RegisterDecl(const_declarations_.back().get());
}

bool Library::ConsumeEnumDeclaration(std::unique_ptr<raw::EnumDeclaration> enum_declaration) {
    std::vector<Enum::Member> members;
    for (auto& member : enum_declaration->members) {
        auto name = Name(member->identifier->location);
        auto value = std::move(member->value);
        members.emplace_back(std::move(name), std::move(value));
    }
    auto type = types::PrimitiveSubtype::Uint32;
    if (enum_declaration->maybe_subtype)
        type = enum_declaration->maybe_subtype->subtype;
    auto name = Name(enum_declaration->identifier->location);

    enum_declarations_.push_back(std::make_unique<Enum>(std::move(name), type, std::move(members)));
    return RegisterDecl(enum_declarations_.back().get());
}

bool Library::ConsumeInterfaceDeclaration(
    std::unique_ptr<raw::InterfaceDeclaration> interface_declaration) {
    auto name = Name(interface_declaration->identifier->location);

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

        SourceLocation method_name = method->identifier->location;

        std::unique_ptr<Interface::Method::Message> maybe_request;
        if (method->maybe_request != nullptr) {
            maybe_request.reset(new Interface::Method::Message());
            for (auto& parameter : method->maybe_request->parameter_list) {
                SourceLocation parameter_name = parameter->identifier->location;
                std::unique_ptr<Type> type;
                if (!ConsumeType(std::move(parameter->type), &type))
                    return false;
                maybe_request->parameters.emplace_back(std::move(type), std::move(parameter_name));
            }
        }

        std::unique_ptr<Interface::Method::Message> maybe_response;
        if (method->maybe_response != nullptr) {
            maybe_response.reset(new Interface::Method::Message());
            for (auto& parameter : method->maybe_response->parameter_list) {
                SourceLocation parameter_name = parameter->identifier->location;
                std::unique_ptr<Type> type;
                if (!ConsumeType(std::move(parameter->type), &type))
                    return false;
                maybe_response->parameters.emplace_back(std::move(type), parameter_name);
            }
        }

        assert(maybe_request != nullptr || maybe_response != nullptr);

        methods.emplace_back(std::move(ordinal), std::move(method_name),
                             std::move(maybe_request), std::move(maybe_response));
    }

    interface_declarations_.push_back(std::make_unique<Interface>(std::move(name), std::move(methods)));
    return RegisterDecl(interface_declarations_.back().get());
}

bool Library::ConsumeStructDeclaration(std::unique_ptr<raw::StructDeclaration> struct_declaration) {
    auto name = Name(struct_declaration->identifier->location);

    for (auto& const_member : struct_declaration->const_members)
        if (!ConsumeConstDeclaration(std::move(const_member)))
            return false;
    for (auto& enum_member : struct_declaration->enum_members)
        if (!ConsumeEnumDeclaration(std::move(enum_member)))
            return false;

    std::vector<Struct::Member> members;
    for (auto& member : struct_declaration->members) {
        std::unique_ptr<Type> type;
        if (!ConsumeType(std::move(member->type), &type))
            return false;
        members.emplace_back(std::move(type),
                             member->identifier->location,
                             std::move(member->maybe_default_value));
    }

    struct_declarations_.push_back(std::make_unique<Struct>(std::move(name), std::move(members)));
    return RegisterDecl(struct_declarations_.back().get());
}

bool Library::ConsumeUnionDeclaration(std::unique_ptr<raw::UnionDeclaration> union_declaration) {
    std::vector<Union::Member> members;
    for (auto& member : union_declaration->members) {
        std::unique_ptr<Type> type;
        if (!ConsumeType(std::move(member->type), &type))
            return false;
        members.emplace_back(std::move(type), member->identifier->location);
    }
    auto name = Name(union_declaration->identifier->location);

    union_declarations_.push_back(std::make_unique<Union>(std::move(name), std::move(members)));
    return RegisterDecl(union_declarations_.back().get());
}

bool Library::ConsumeFile(std::unique_ptr<raw::File> file) {
    // All fidl files in a library should agree on the library name.
    if (file->identifier->components.size() != 1) {
        return false;
    }
    auto library_name = file->identifier->components[0]->location;

    if (library_name_.valid()) {
        StringView current_name = library_name_.data();
        StringView new_name = library_name.data();
        if (current_name != new_name) {
            return false;
        }
    } else {
        library_name_ = library_name;
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

// Library resolution is concerned with resolving identifiers to their
// declarations, and with computing type sizes and alignments.

Decl* Library::LookupType(const flat::Type* type) {
    for (;;) {
        switch (type->kind) {
        case flat::Type::Kind::String:
        case flat::Type::Kind::Handle:
        case flat::Type::Kind::Request:
        case flat::Type::Kind::Primitive:
        case flat::Type::Kind::Vector:
            return nullptr;
        case flat::Type::Kind::Array: {
            type = static_cast<const flat::ArrayType*>(type)->element_type.get();
            continue;
        }
        case flat::Type::Kind::Identifier: {
            auto identifier_type = static_cast<const flat::IdentifierType*>(type);
            if (identifier_type->nullability == types::Nullability::Nullable) {
                return nullptr;
            }
            return LookupType(identifier_type->name);
        }
        }
    }
}

Decl* Library::LookupType(const Name& name) {
    auto iter = declarations_.find(&name);
    if (iter == declarations_.end()) {
        return nullptr;
    }
    return iter->second;
}

// An edge from D1 to D2 means that a C needs to see the declaration
// of D1 before the declaration of D2. For instance, given the fidl
//     struct D2 { D1 d; };
//     struct D1 { int32 x; };
// D1 has an edge pointing to D2. Note that struct and union pointers,
// unlike inline structs or unions, do not have dependency edges.
std::set<Decl*> Library::DeclDependencies(Decl* decl) {
    std::set<Decl*> edges;
    auto maybe_add_decl = [this, &edges](const std::unique_ptr<flat::Type>& type) {
        auto type_decl = LookupType(type.get());
        if (type_decl != nullptr) {
            edges.insert(type_decl);
        }
    };
    switch (decl->kind) {
    case Decl::Kind::kConst:
    case Decl::Kind::kEnum:
        break;
    case Decl::Kind::kInterface: {
        auto interface_decl = static_cast<const Interface*>(decl);
        for (const auto& method : interface_decl->methods) {
            if (method.maybe_request != nullptr) {
                for (const auto& parameter : method.maybe_request->parameters) {
                    maybe_add_decl(parameter.type);
                }
            }
            if (method.maybe_response != nullptr) {
                for (const auto& parameter : method.maybe_response->parameters) {
                    maybe_add_decl(parameter.type);
                }
            }
        }
        break;
    }
    case Decl::Kind::kStruct: {
        auto struct_decl = static_cast<const Struct*>(decl);
        for (const auto& member : struct_decl->members) {
            maybe_add_decl(member.type);
        }
        break;
    }
    case Decl::Kind::kUnion: {
        auto union_decl = static_cast<const Union*>(decl);
        for (const auto& member : union_decl->members) {
            maybe_add_decl(member.type);
        }
        break;
    }
    }
    return edges;
}

bool Library::SortDeclarations() {
    // |degree| is the number of undeclared dependencies for each decl.
    std::map<Decl*, uint32_t> degrees;
    // |inverse_dependencies| records the decls that depend on each decl.
    std::map<Decl*, std::vector<Decl*>> inverse_dependencies;
    for (auto& name_and_decl : declarations_) {
        Decl* decl = name_and_decl.second;
        degrees[decl] = 0u;
    }
    for (auto& name_and_decl : declarations_) {
        Decl* decl = name_and_decl.second;
        auto deps = DeclDependencies(decl);
        degrees[decl] += deps.size();
        for (Decl* dep : deps) {
            inverse_dependencies[dep].push_back(decl);
        }
    }

    // Start with all decls that have no incoming edges.
    std::vector<Decl*> decls_without_deps;
    for (const auto& decl_and_degree : degrees) {
        if (decl_and_degree.second == 0u) {
            decls_without_deps.push_back(decl_and_degree.first);
        }
    }

    while (!decls_without_deps.empty()) {
        // Pull one out of the queue.
        auto decl = decls_without_deps.back();
        decls_without_deps.pop_back();
        assert(degrees[decl] == 0u);
        declaration_order_.push_back(decl);

        // Decrement the incoming degree of all the other decls it
        // points to.
        auto& inverse_deps = inverse_dependencies[decl];
        for (Decl* inverse_dep : inverse_deps) {
            uint32_t& degree = degrees[inverse_dep];
            assert(degree != 0u);
            degree -= 1;
            if (degree == 0u)
                decls_without_deps.push_back(inverse_dep);
        }
    }

    if (declaration_order_.size() != degrees.size()) {
        // We didn't visit all the edges! There was a cycle.
        return false;
    }

    assert(declaration_order_.size() != 0u);
    return true;
}

bool Library::ResolveConst(Const* const_declaration) {
    TypeShape typeshape;
    if (!ResolveType(const_declaration->type.get(), &typeshape)) {
        return false;
    }
    // TODO(TO-702) Resolve const declarations.
    return true;
}

bool Library::ResolveEnum(Enum* enum_declaration) {
    switch (enum_declaration->type) {
    case types::PrimitiveSubtype::Int8:
    case types::PrimitiveSubtype::Int16:
    case types::PrimitiveSubtype::Int32:
    case types::PrimitiveSubtype::Int64:
    case types::PrimitiveSubtype::Uint8:
    case types::PrimitiveSubtype::Uint16:
    case types::PrimitiveSubtype::Uint32:
    case types::PrimitiveSubtype::Uint64:
        // These are allowed as enum subtypes. Resolve the size and alignment.
        enum_declaration->typeshape = PrimitiveTypeShape(enum_declaration->type);
        break;

    case types::PrimitiveSubtype::Bool:
    case types::PrimitiveSubtype::Status:
    case types::PrimitiveSubtype::Float32:
    case types::PrimitiveSubtype::Float64:
        // These are not allowed as enum subtypes.
        return false;
    }

    // TODO(TO-702) Validate values.
    return true;
}

bool Library::ResolveInterface(Interface* interface_declaration) {
    // TODO(TO-703) Add subinterfaces here.
    Scope<StringView> name_scope;
    Scope<uint32_t> ordinal_scope;
    for (auto& method : interface_declaration->methods) {
        if (!name_scope.Insert(method.name.data()))
            return false;
        if (!ordinal_scope.Insert(method.ordinal.Value()))
            return false;
        if (method.maybe_request != nullptr) {
            Scope<StringView> request_scope;
            std::vector<FieldShape*> request_struct;
            for (auto& param : method.maybe_request->parameters) {
                if (!request_scope.Insert(param.name.data()))
                    return false;
                if (!ResolveType(param.type.get(), &param.fieldshape.Typeshape()))
                    return false;
                request_struct.push_back(&param.fieldshape);
            }
            method.maybe_request->typeshape = FidlStructTypeShape(&request_struct);
        }
        if (method.maybe_response != nullptr) {
            Scope<StringView> response_scope;
            std::vector<FieldShape*> response_struct;
            for (auto& param : method.maybe_response->parameters) {
                if (!response_scope.Insert(param.name.data()))
                    return false;
                if (!ResolveType(param.type.get(), &param.fieldshape.Typeshape()))
                    return false;
                response_struct.push_back(&param.fieldshape);
            }
            method.maybe_response->typeshape = FidlStructTypeShape(&response_struct);
        }
    }
    return true;
}

bool Library::ResolveStruct(Struct* struct_declaration) {
    Scope<StringView> scope;
    std::vector<FieldShape*> fidl_struct;
    for (auto& member : struct_declaration->members) {
        if (!scope.Insert(member.name.data()))
            return false;
        if (!ResolveType(member.type.get(), &member.fieldshape.Typeshape()))
            return false;
        fidl_struct.push_back(&member.fieldshape);
    }

    struct_declaration->typeshape = FidlStructTypeShape(&fidl_struct);

    return true;
}

bool Library::ResolveUnion(Union* union_declaration) {
    Scope<StringView> scope;
    for (auto& member : union_declaration->members) {
        if (!scope.Insert(member.name.data()))
            return false;
        if (!ResolveType(member.type.get(), &member.fieldshape.Typeshape()))
            return false;
    }

    auto tag = FieldShape(kUint32TypeShape);
    auto members = FieldShape(CUnionTypeShape(union_declaration->members));
    std::vector<FieldShape*> fidl_union = {&tag, &members};

    union_declaration->typeshape = CStructTypeShape(&fidl_union);

    // This is either 4 or 8, depending on whether any union members
    // have alignment 8.
    auto offset = members.Offset();
    for (auto& member : union_declaration->members) {
        member.fieldshape.SetOffset(offset);
    }

    return true;
}

bool Library::Resolve() {
    if (!SortDeclarations()) {
        return false;
    }

    // We process declarations in topologically sorted order. For
    // example, we process a struct member's type before the entire
    // struct.
    for (Decl* decl : declaration_order_) {
        switch (decl->kind) {
        case Decl::Kind::kConst: {
            auto const_decl = static_cast<Const*>(decl);
            if (!ResolveConst(const_decl)) {
                return false;
            }
            break;
        }
        case Decl::Kind::kEnum: {
            auto enum_decl = static_cast<Enum*>(decl);
            if (!ResolveEnum(enum_decl)) {
                return false;
            }
            break;
        }
        case Decl::Kind::kInterface: {
            auto interface_decl = static_cast<Interface*>(decl);
            if (!ResolveInterface(interface_decl)) {
                return false;
            }
            break;
        }
        case Decl::Kind::kStruct: {
            auto struct_decl = static_cast<Struct*>(decl);
            if (!ResolveStruct(struct_decl)) {
                return false;
            }
            break;
        }
        case Decl::Kind::kUnion: {
            auto union_decl = static_cast<Union*>(decl);
            if (!ResolveUnion(union_decl)) {
                return false;
            }
            break;
        }
        default:
            abort();
        }
    }

    return true;
}

bool Library::ResolveArrayType(flat::ArrayType* array_type, TypeShape* out_typeshape) {
    TypeShape element_typeshape;
    if (!ResolveType(array_type->element_type.get(), &element_typeshape))
        return false;
    *out_typeshape = ArrayTypeShape(element_typeshape, array_type->element_count.Value());
    return true;
}

bool Library::ResolveVectorType(flat::VectorType* vector_type, TypeShape* out_typeshape) {
    *out_typeshape = VectorTypeShape();
    return true;
}

bool Library::ResolveStringType(flat::StringType* string_type, TypeShape* out_typeshape) {
    *out_typeshape = StringTypeShape();
    return true;
}

bool Library::ResolveHandleType(flat::HandleType* handle_type, TypeShape* out_typeshape) {
    // Nothing to check.
    *out_typeshape = kHandleTypeShape;
    return true;
}

bool Library::ResolveRequestType(flat::RequestType* request_type, TypeShape* out_typeshape) {
    auto named_decl = LookupType(request_type->name);
    if (!named_decl || named_decl->kind != Decl::Kind::kInterface)
        return false;

    *out_typeshape = kHandleTypeShape;
    return true;
}

bool Library::ResolvePrimitiveType(flat::PrimitiveType* primitive_type,
                                   TypeShape* out_typeshape) {
    *out_typeshape = PrimitiveTypeShape(primitive_type->subtype);
    return true;
}

bool Library::ResolveIdentifierType(flat::IdentifierType* identifier_type,
                                    TypeShape* out_typeshape) {
    TypeShape typeshape;

    auto named_decl = LookupType(identifier_type->name);
    if (!named_decl)
        return false;

    switch (named_decl->kind) {
    case Decl::Kind::kConst: {
        // A constant isn't a type!
        return false;
    }
    case Decl::Kind::kEnum: {
        if (identifier_type->nullability == types::Nullability::Nullable) {
            // Enums aren't nullable!
            return false;
        } else {
            typeshape = static_cast<const Enum*>(named_decl)->typeshape;
        }
        break;
    }
    case Decl::Kind::kInterface: {
        typeshape = kHandleTypeShape;
        break;
    }
    case Decl::Kind::kStruct: {
        if (identifier_type->nullability == types::Nullability::Nullable) {
           typeshape = kPointerTypeShape;
        } else {
           typeshape = static_cast<const Struct*>(named_decl)->typeshape;
        }
        break;
    }
    case Decl::Kind::kUnion: {
        if (identifier_type->nullability == types::Nullability::Nullable) {
            typeshape = kPointerTypeShape;
        } else {
            typeshape = static_cast<const Union*>(named_decl)->typeshape;
        }
        break;
    }
    default: {
        abort();
    }
    }

    identifier_type->size = typeshape.Size();
    *out_typeshape = typeshape;
    return true;
}

bool Library::ResolveType(Type* type, TypeShape* out_typeshape) {
    switch (type->kind) {
    case Type::Kind::Array: {
        auto array_type = static_cast<ArrayType*>(type);
        return ResolveArrayType(array_type, out_typeshape);
    }

    case Type::Kind::Vector: {
        auto vector_type = static_cast<VectorType*>(type);
        return ResolveVectorType(vector_type, out_typeshape);
    }

    case Type::Kind::String: {
        auto string_type = static_cast<StringType*>(type);
        return ResolveStringType(string_type, out_typeshape);
    }

    case Type::Kind::Handle: {
        auto handle_type = static_cast<HandleType*>(type);
        return ResolveHandleType(handle_type, out_typeshape);
    }

    case Type::Kind::Request: {
        auto request_type = static_cast<RequestType*>(type);
        return ResolveRequestType(request_type, out_typeshape);
    }

    case Type::Kind::Primitive: {
        auto primitive_type = static_cast<PrimitiveType*>(type);
        return ResolvePrimitiveType(primitive_type, out_typeshape);
    }

    case Type::Kind::Identifier: {
        auto identifier_type = static_cast<IdentifierType*>(type);
        return ResolveIdentifierType(identifier_type, out_typeshape);
    }
    }
}

} // namespace flat
} // namespace fidl
