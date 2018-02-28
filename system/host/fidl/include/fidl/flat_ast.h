// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FLAT_AST_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FLAT_AST_H_

#include <errno.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <set>
#include <vector>

#include "raw_ast.h"
#include "type_shape.h"

namespace fidl {
namespace flat {

struct Ordinal {
    Ordinal(std::unique_ptr<raw::NumericLiteral> literal, uint32_t value)
        : literal_(std::move(literal)), value_(value) {}

    uint32_t Value() const { return value_; }

private:
    std::unique_ptr<raw::NumericLiteral> literal_;
    uint32_t value_;
};

// TODO(TO-701) Handle multipart names.
struct Name {
    Name()
        : name_(nullptr) {}

    explicit Name(std::unique_ptr<raw::Identifier> name)
        : name_(std::move(name)) {}

    Name(Name&&) = default;
    Name& operator=(Name&&) = default;

    const raw::Identifier* get() const { return name_.get(); }

    bool operator==(const Name& other) const {
        return name_->location.data() == other.name_->location.data();
    }
    bool operator!=(const Name& other) const {
        return name_->location.data() != other.name_->location.data();
    }

    bool operator<(const Name& other) const {
        return name_->location.data() < other.name_->location.data();
    }

private:
    std::unique_ptr<raw::Identifier> name_;
};

struct NamePtrCompare {
    bool operator()(const Name* left, const Name* right) const {
        return *left < *right;
    }
};

struct Decl {
    virtual ~Decl() {}

    enum struct Kind {
        kConst,
        kEnum,
        kInterface,
        kStruct,
        kUnion,
    };

    Decl(Kind kind, Name name)
        : kind(kind), name(std::move(name)) {}

    Decl(Decl&&) = default;
    Decl& operator=(Decl&&) = default;

    const Kind kind;
    const Name name;
};

struct Const : public Decl {
    Const(Name name, std::unique_ptr<raw::Type> type, std::unique_ptr<raw::Constant> value)
        : Decl(Kind::kConst, std::move(name)), type(std::move(type)), value(std::move(value)) {}
    std::unique_ptr<raw::Type> type;
    std::unique_ptr<raw::Constant> value;
};

struct Enum : public Decl {
    struct Member {
        Member(Name name, std::unique_ptr<raw::Constant> value)
            : name(std::move(name)), value(std::move(value)) {}
        Name name;
        std::unique_ptr<raw::Constant> value;
    };

    Enum(Name name, std::unique_ptr<raw::PrimitiveType> type, std::vector<Member> members)
        : Decl(Kind::kEnum, std::move(name)), type(std::move(type)), members(std::move(members)) {}

    std::unique_ptr<raw::PrimitiveType> type;
    std::vector<Member> members;
    TypeShape typeshape;
};

struct Interface : public Decl {
    struct Method {
        struct Parameter {
            Parameter(std::unique_ptr<raw::Type> type, std::unique_ptr<raw::Identifier> name)
                : type(std::move(type)), name(std::move(name)) {}
            std::unique_ptr<raw::Type> type;
            std::unique_ptr<raw::Identifier> name;
            // TODO(TO-758) Compute these.
            FieldShape fieldshape;
        };

        Method(Method&&) = default;
        Method& operator=(Method&&) = default;

        Method(Ordinal ordinal, std::unique_ptr<raw::Identifier> name, bool has_request,
               std::vector<Parameter> maybe_request, bool has_response,
               std::vector<Parameter> maybe_response)
            : ordinal(std::move(ordinal)), name(std::move(name)), has_request(has_request),
              maybe_request(std::move(maybe_request)), has_response(has_response),
              maybe_response(std::move(maybe_response)) {
            assert(has_request || has_response);
        }

        Ordinal ordinal;
        std::unique_ptr<raw::Identifier> name;
        bool has_request;
        std::vector<Parameter> maybe_request;
        // TODO(TO-758,TO-825) Compute these.
        TypeShape maybe_request_typeshape;
        bool has_response;
        std::vector<Parameter> maybe_response;
        // TODO(TO-758,TO-825) Compute these.
        TypeShape maybe_response_typeshape;
    };

    Interface(Name name, std::vector<Method> methods)
        : Decl(Kind::kInterface, std::move(name)), methods(std::move(methods)) {}

    std::vector<Method> methods;
};

struct Struct : public Decl {
    struct Member {
        Member(std::unique_ptr<raw::Type> type, std::unique_ptr<raw::Identifier> name,
               std::unique_ptr<raw::Constant> maybe_default_value)
            : type(std::move(type)), name(std::move(name)),
              maybe_default_value(std::move(maybe_default_value)) {}
        std::unique_ptr<raw::Type> type;
        std::unique_ptr<raw::Identifier> name;
        std::unique_ptr<raw::Constant> maybe_default_value;
        // TODO(TO-758) Compute these.
        FieldShape fieldshape;
    };

    Struct(Name name, std::vector<Member> members)
        : Decl(Kind::kStruct, std::move(name)), members(std::move(members)) {}

    std::vector<Member> members;
    TypeShape typeshape;
};

struct Union : public Decl {
    struct Member {
        Member(std::unique_ptr<raw::Type> type, std::unique_ptr<raw::Identifier> name)
            : type(std::move(type)), name(std::move(name)) {}
        std::unique_ptr<raw::Type> type;
        std::unique_ptr<raw::Identifier> name;
        // TODO(TO-758) Compute these.
        FieldShape fieldshape;
    };

    Union(Name name, std::vector<Member> members)
        : Decl(Kind::kUnion, std::move(name)), members(std::move(members)) {}

    std::vector<Member> members;
    TypeShape typeshape;
};

class Library {
public:
    bool ConsumeFile(std::unique_ptr<raw::File> file);
    bool Resolve();

private:
    bool RegisterDecl(Decl* decl);

    bool ConsumeConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const_declaration);
    bool ConsumeEnumDeclaration(std::unique_ptr<raw::EnumDeclaration> enum_declaration);
    bool
    ConsumeInterfaceDeclaration(std::unique_ptr<raw::InterfaceDeclaration> interface_declaration);
    bool ConsumeStructDeclaration(std::unique_ptr<raw::StructDeclaration> struct_declaration);
    bool ConsumeUnionDeclaration(std::unique_ptr<raw::UnionDeclaration> union_declaration);

    // Returns nullptr when |type| does not correspond directly to a
    // declaration. For example, if |type| refers to int32 or if it is
    // a struct pointer, this will return null. If it is a struct, it
    // will return a pointer to the declaration of the type.
    Decl* LookupType(const raw::Type* type);

    // Returns nullptr when the |identifier| cannot be resolved to a
    // Name. Otherwise it returns the declaration.
    Decl* LookupType(const raw::CompoundIdentifier* identifier);

    std::set<Decl*> DeclDependencies(Decl* decl);

    bool SortDeclarations();

    bool ResolveConst(Const* const_declaration);
    bool ResolveEnum(Enum* enum_declaration);
    bool ResolveInterface(Interface* interface_declaration);
    bool ResolveStruct(Struct* struct_declaration);
    bool ResolveUnion(Union* union_declaration);

    bool ResolveArrayType(const raw::ArrayType& array_type, TypeShape* out_type_metadata);
    bool ResolveVectorType(const raw::VectorType& vector_type, TypeShape* out_type_metadata);
    bool ResolveStringType(const raw::StringType& string_type, TypeShape* out_type_metadata);
    bool ResolveHandleType(const raw::HandleType& handle_type, TypeShape* out_type_metadata);
    bool ResolveRequestType(const raw::RequestType& request_type, TypeShape* out_type_metadata);
    bool ResolvePrimitiveType(const raw::PrimitiveType& primitive_type,
                              TypeShape* out_type_metadata);
    bool ResolveIdentifierType(const raw::IdentifierType& identifier_type,
                               TypeShape* out_type_metadata);
    bool ResolveType(const raw::Type* type, TypeShape* out_type_metadata);

public:
    // TODO(TO-702) Add a validate literal function. Some things
    // (e.g. array indexes) want to check the value but print the
    // constant, say.
    template <typename IntType>
    bool ParseIntegerLiteral(const raw::NumericLiteral* literal, IntType* out_value) {
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
    bool ParseIntegerConstant(const raw::Constant* constant, IntType* out_value) {
        if (!constant) {
            return false;
        }
        switch (constant->kind) {
        case raw::Constant::Kind::Identifier: {
            auto identifier_constant = static_cast<const raw::IdentifierConstant*>(constant);
            auto identifier = identifier_constant->identifier.get();
            // TODO(TO-702) Actually resolve this.
            static_cast<void>(identifier);
            *out_value = static_cast<IntType>(123);
            return true;
        }
        case raw::Constant::Kind::Literal: {
            auto literal_constant = static_cast<const raw::LiteralConstant*>(constant);
            switch (literal_constant->literal->kind) {
            case raw::Literal::Kind::String:
            case raw::Literal::Kind::True:
            case raw::Literal::Kind::False:
            case raw::Literal::Kind::Default: {
                return false;
            }

            case raw::Literal::Kind::Numeric: {
                auto numeric_literal =
                    static_cast<const raw::NumericLiteral*>(literal_constant->literal.get());
                return ParseIntegerLiteral<IntType>(numeric_literal, out_value);
            }
            }
        }
        }
    }

    std::unique_ptr<raw::Identifier> library_name_;

    std::vector<std::unique_ptr<Const>> const_declarations_;
    std::vector<std::unique_ptr<Enum>> enum_declarations_;
    std::vector<std::unique_ptr<Interface>> interface_declarations_;
    std::vector<std::unique_ptr<Struct>> struct_declarations_;
    std::vector<std::unique_ptr<Union>> union_declarations_;

    // All Decl pointers here are non-null and are owned by the
    // various foo_declarations_.
    std::vector<Decl*> declaration_order_;

private:
    // All Name and Decl pointers here are non-null and are owned by the
    // various foo_declarations_.
    std::map<const Name*, Decl*, NamePtrCompare> declarations_;
};

} // namespace flat
} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FLAT_AST_H_
