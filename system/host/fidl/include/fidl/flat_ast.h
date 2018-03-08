// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FLAT_AST_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FLAT_AST_H_

#include <errno.h>
#include <stdint.h>

#include <limits>
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

template <typename IntType>
struct IntConstant {
    IntConstant(std::unique_ptr<raw::Constant> raw_constant, IntType value)
        : raw_constant_(std::move(raw_constant)), value_(value) {}

    explicit IntConstant(IntType value) : value_(value) {}

    IntConstant() : value_(0) {}

    IntType Value() const { return value_; }

    static IntConstant Max() { return IntConstant(std::numeric_limits<IntType>::max()); }

private:
    std::unique_ptr<raw::Constant> raw_constant_;
    IntType value_;
};

using Size = IntConstant<uint64_t>;

// TODO(TO-701) Handle multipart names.
struct Name {
    Name()
        : name_(SourceLocation()) {}

    explicit Name(SourceLocation name)
        : name_(name) {}

    Name(Name&&) = default;
    Name& operator=(Name&&) = default;

    StringView data() const { return name_.data(); }

    bool operator==(const Name& other) const {
        return name_.data() == other.name_.data();
    }
    bool operator!=(const Name& other) const {
        return name_.data() != other.name_.data();
    }

    bool operator<(const Name& other) const {
        return name_.data() < other.name_.data();
    }

private:
    SourceLocation name_;
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

struct Type {
    virtual ~Type() {}

    enum struct Kind {
        Array,
        Vector,
        String,
        Handle,
        Request,
        Primitive,
        Identifier,
    };

    explicit Type(Kind kind, uint64_t size)
        : kind(kind), size(size) {}

    const Kind kind;
    // Set at construction time for most Types. Identifier types get
    // this set later, during compilation.
    uint64_t size;
};

struct ArrayType : public Type {
    ArrayType(uint64_t size, std::unique_ptr<Type> element_type, Size element_count)
        : Type(Kind::Array, size),
          element_type(std::move(element_type)),
          element_count(std::move(element_count)) {}

    std::unique_ptr<Type> element_type;
    Size element_count;
};

struct VectorType : public Type {
    VectorType(std::unique_ptr<Type> element_type, Size element_count,
               types::Nullability nullability)
        : Type(Kind::Vector, 16u),
          element_type(std::move(element_type)),
          element_count(std::move(element_count)), nullability(nullability) {}

    std::unique_ptr<Type> element_type;
    Size element_count;
    types::Nullability nullability;
};

struct StringType : public Type {
    StringType(Size max_size, types::Nullability nullability)
        : Type(Kind::String, 16u), max_size(std::move(max_size)),
          nullability(nullability) {}

    Size max_size;
    types::Nullability nullability;
};

struct HandleType : public Type {
    HandleType(types::HandleSubtype subtype, types::Nullability nullability)
        : Type(Kind::Handle, 4u), subtype(subtype), nullability(nullability) {}

    types::HandleSubtype subtype;
    types::Nullability nullability;
};

struct RequestType : public Type {
    RequestType(Name name, types::Nullability nullability)
        : Type(Kind::Request, 4u), name(std::move(name)), nullability(nullability) {}

    Name name;
    types::Nullability nullability;
};

struct PrimitiveType : public Type {
    static uint64_t SubtypeSize(types::PrimitiveSubtype subtype) {
        switch (subtype) {
        case types::PrimitiveSubtype::Bool:
        case types::PrimitiveSubtype::Int8:
        case types::PrimitiveSubtype::Uint8:
            return 1u;

        case types::PrimitiveSubtype::Int16:
        case types::PrimitiveSubtype::Uint16:
            return 2u;

        case types::PrimitiveSubtype::Float32:
        case types::PrimitiveSubtype::Status:
        case types::PrimitiveSubtype::Int32:
        case types::PrimitiveSubtype::Uint32:
            return 4u;

        case types::PrimitiveSubtype::Float64:
        case types::PrimitiveSubtype::Int64:
        case types::PrimitiveSubtype::Uint64:
            return 8u;
        }
    }

    explicit PrimitiveType(types::PrimitiveSubtype subtype)
        : Type(Kind::Primitive, SubtypeSize(subtype)), subtype(subtype) { }

    types::PrimitiveSubtype subtype;
};

struct IdentifierType : public Type {
    IdentifierType(Name name, types::Nullability nullability)
        : Type(Kind::Identifier, 0u), name(std::move(name)), nullability(nullability) {}

    Name name;
    types::Nullability nullability;
};

struct Const : public Decl {
    Const(Name name, std::unique_ptr<Type> type, std::unique_ptr<raw::Constant> value)
        : Decl(Kind::kConst, std::move(name)), type(std::move(type)), value(std::move(value)) {}
    std::unique_ptr<Type> type;
    std::unique_ptr<raw::Constant> value;
};

struct Enum : public Decl {
    struct Member {
        Member(SourceLocation name, std::unique_ptr<raw::Constant> value)
            : name(name), value(std::move(value)) {}
        SourceLocation name;
        std::unique_ptr<raw::Constant> value;
    };

    Enum(Name name, types::PrimitiveSubtype type, std::vector<Member> members)
        : Decl(Kind::kEnum, std::move(name)), type(type), members(std::move(members)) {}

    types::PrimitiveSubtype type;
    std::vector<Member> members;
    TypeShape typeshape;
};

struct Interface : public Decl {
    struct Method {
        struct Parameter {
            Parameter(std::unique_ptr<Type> type, SourceLocation name)
                : type(std::move(type)), name(std::move(name)) {}
            std::unique_ptr<Type> type;
            SourceLocation name;
            // TODO(TO-758) Compute these.
            FieldShape fieldshape;
        };

        struct Message {
            std::vector<Parameter> parameters;
            TypeShape typeshape;
        };

        Method(Method&&) = default;
        Method& operator=(Method&&) = default;

        Method(Ordinal ordinal, SourceLocation name,
               std::unique_ptr<Message> maybe_request,
               std::unique_ptr<Message> maybe_response)
            : ordinal(std::move(ordinal)), name(std::move(name)),
              maybe_request(std::move(maybe_request)),
              maybe_response(std::move(maybe_response)) {
            assert(this->maybe_request != nullptr || this->maybe_response != nullptr);
        }

        Ordinal ordinal;
        SourceLocation name;
        std::unique_ptr<Message> maybe_request;
        std::unique_ptr<Message> maybe_response;
    };

    Interface(Name name, std::vector<Method> methods)
        : Decl(Kind::kInterface, std::move(name)), methods(std::move(methods)) {}

    std::vector<Method> methods;
};

struct Struct : public Decl {
    struct Member {
        Member(std::unique_ptr<Type> type, SourceLocation name,
               std::unique_ptr<raw::Constant> maybe_default_value)
            : type(std::move(type)), name(std::move(name)),
              maybe_default_value(std::move(maybe_default_value)) {}
        std::unique_ptr<Type> type;
        SourceLocation name;
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
        Member(std::unique_ptr<Type> type, SourceLocation name)
            : type(std::move(type)), name(std::move(name)) {}
        std::unique_ptr<Type> type;
        SourceLocation name;
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
    bool ParseSize(std::unique_ptr<raw::Constant> raw_constant, Size* out_size);

    bool RegisterDecl(Decl* decl);

    bool ConsumeType(std::unique_ptr<raw::Type> type, std::unique_ptr<Type>* out_type);

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
    Decl* LookupType(const flat::Type* type);

    // Returns nullptr when the |name| cannot be resolved to a
    // Name. Otherwise it returns the declaration.
    Decl* LookupType(const Name& name);

    std::set<Decl*> DeclDependencies(Decl* decl);

    bool SortDeclarations();

    bool ResolveConst(Const* const_declaration);
    bool ResolveEnum(Enum* enum_declaration);
    bool ResolveInterface(Interface* interface_declaration);
    bool ResolveStruct(Struct* struct_declaration);
    bool ResolveUnion(Union* union_declaration);

    bool ResolveArrayType(ArrayType* array_type, TypeShape* out_type_metadata);
    bool ResolveVectorType(VectorType* vector_type, TypeShape* out_type_metadata);
    bool ResolveStringType(StringType* string_type, TypeShape* out_type_metadata);
    bool ResolveHandleType(HandleType* handle_type, TypeShape* out_type_metadata);
    bool ResolveRequestType(RequestType* request_type, TypeShape* out_type_metadata);
    bool ResolvePrimitiveType(PrimitiveType* primitive_type, TypeShape* out_type_metadata);
    bool ResolveIdentifierType(IdentifierType* identifier_type, TypeShape* out_type_metadata);
    bool ResolveType(Type* type, TypeShape* out_type_metadata);

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
            // TODO(TO-701) Support more parts of names.
            Name name(identifier->components[0]->location);
            auto decl = LookupType(name);
            if (!decl || decl->kind != Decl::Kind::kConst)
                return false;
            return ParseIntegerConstant(static_cast<Const*>(decl)->value.get(), out_value);
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

    SourceLocation library_name_;

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
