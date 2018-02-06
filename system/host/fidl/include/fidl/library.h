// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_LIBRARY_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_LIBRARY_H_

#include <errno.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ast.h"
#include "coded_ast.h"
#include "flat_ast.h"
#include "identifier_table.h"
#include "source_manager.h"
#include "type_shape.h"

namespace fidl {

class Library {
public:
    bool ConsumeFile(std::unique_ptr<ast::File> file);
    bool Resolve();

private:
    bool ConsumeConstDeclaration(std::unique_ptr<ast::ConstDeclaration> const_declaration);
    bool ConsumeEnumDeclaration(std::unique_ptr<ast::EnumDeclaration> enum_declaration);
    bool
    ConsumeInterfaceDeclaration(std::unique_ptr<ast::InterfaceDeclaration> interface_declaration);
    bool ConsumeStructDeclaration(std::unique_ptr<ast::StructDeclaration> struct_declaration);
    bool ConsumeUnionDeclaration(std::unique_ptr<ast::UnionDeclaration> union_declaration);

    bool RegisterTypeName(const flat::Name& name);

    bool ResolveConst(const flat::Const& const_declaration);
    bool ResolveEnum(const flat::Enum& enum_declaration);
    bool ResolveInterface(const flat::Interface& interface_declaration);
    bool ResolveStruct(const flat::Struct& struct_declaration);
    bool ResolveUnion(const flat::Union& union_declaration);

    bool ResolveArrayType(const ast::ArrayType& array_type, TypeShape* out_type_metadata);
    bool ResolveVectorType(const ast::VectorType& vector_type, TypeShape* out_type_metadata);
    bool ResolveStringType(const ast::StringType& string_type, TypeShape* out_type_metadata);
    bool ResolveHandleType(const ast::HandleType& handle_type, TypeShape* out_type_metadata);
    bool ResolveRequestType(const ast::RequestType& request_type, TypeShape* out_type_metadata);
    bool ResolvePrimitiveType(const ast::PrimitiveType& primitive_type,
                              TypeShape* out_type_metadata);
    bool ResolveIdentifierType(const ast::IdentifierType& identifier_type,
                               TypeShape* out_type_metadata);
    bool ResolveType(const ast::Type* type) {
        TypeShape type_metadata;
        return ResolveType(type, &type_metadata);
    }
    bool ResolveType(const ast::Type* type, TypeShape* out_type_metadata);
    bool ResolveTypeName(const ast::CompoundIdentifier* name);
    bool RegisterResolvedType(const flat::Name& name, TypeShape type_metadata);

    bool LookupTypeShape(const flat::Name& name, TypeShape* out_typeshape);

    const coded::Type* LookupIdentifierType(const ast::IdentifierType* identifier_type);

    void MaybeCreateCodingField(std::string field_name, uint32_t offset, const ast::Type* type,
                                std::vector<coded::Field>* fields);

public:
    // TODO(TO-702) Add a validate literal function. Some things
    // (e.g. array indexes) want to check the value but print the
    // constant, say.
    template <typename IntType>
    bool ParseIntegerLiteral(const ast::NumericLiteral* literal, IntType* out_value) {
        if (!literal) {
            return false;
        }
        auto data = literal->location.data();
        std::string string_data(data.data(), data.data() + data.size());
        if (std::is_unsigned<IntType>::value) {
            errno = 0;
            unsigned long long value = strtoull(string_data.data(), nullptr, 0);
            if (errno != 0)
                return false;
            if (value > std::numeric_limits<IntType>::max())
                return false;
            *out_value = static_cast<IntType>(value);
        } else {
            errno = 0;
            long long value = strtoll(string_data.data(), nullptr, 0);
            if (errno != 0) {
                return false;
            }
            if (value > std::numeric_limits<IntType>::max()) {
                return false;
            }
            if (value < std::numeric_limits<IntType>::min()) {
                return false;
            }
            *out_value = static_cast<IntType>(value);
        }
        return true;
    }

    template <typename IntType>
    bool ParseIntegerConstant(const ast::Constant* constant, IntType* out_value) {
        if (!constant) {
            return false;
        }
        switch (constant->kind) {
        case ast::Constant::Kind::Identifier: {
            auto identifier_constant = static_cast<const ast::IdentifierConstant*>(constant);
            auto identifier = identifier_constant->identifier.get();
            // TODO(TO-702) Actually resolve this.
            static_cast<void>(identifier);
            *out_value = static_cast<IntType>(123);
            return true;
        }
        case ast::Constant::Kind::Literal: {
            auto literal_constant = static_cast<const ast::LiteralConstant*>(constant);
            switch (literal_constant->literal->kind) {
            case ast::Literal::Kind::String:
            case ast::Literal::Kind::True:
            case ast::Literal::Kind::False:
            case ast::Literal::Kind::Default: {
                return false;
            }

            case ast::Literal::Kind::Numeric: {
                auto numeric_literal =
                    static_cast<const ast::NumericLiteral*>(literal_constant->literal.get());
                return ParseIntegerLiteral<IntType>(numeric_literal, out_value);
            }
            }
        }
        }
    }

    std::vector<flat::Const> const_declarations_;
    std::vector<flat::Enum> enum_declarations_;
    std::vector<flat::Interface> interface_declarations_;
    std::vector<flat::Struct> struct_declarations_;
    std::vector<flat::Union> union_declarations_;

private:
    std::set<flat::Name> registered_types_;
    std::map<flat::Name, TypeShape> resolved_types_;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_LIBRARY_H_
