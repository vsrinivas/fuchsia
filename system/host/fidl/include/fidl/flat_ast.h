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

#include "error_reporter.h"
#include "raw_ast.h"
#include "type_shape.h"

namespace fidl {
namespace flat {

template <typename T>
struct PtrCompare {
    bool operator()(const T* left, const T* right) const {
        return *left < *right;
    }
};

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

    explicit IntConstant(IntType value)
        : value_(value) {}

    IntConstant()
        : value_(0) {}

    IntType Value() const { return value_; }

    static IntConstant Max() { return IntConstant(std::numeric_limits<IntType>::max()); }

private:
    std::unique_ptr<raw::Constant> raw_constant_;
    IntType value_;
};

using Size = IntConstant<uint32_t>;

struct Decl;
class Library;

// This is needed (for now) to work around declaration order issues.
StringView LibraryName(const Library* library);

// TODO(TO-701) Handle multipart names.
struct Name {
    Name()
        : name_(SourceLocation()) {}

    Name(const Library* library, std::vector<SourceLocation> nested_decls, SourceLocation name)
        : library_(library), nested_decls_(std::move(nested_decls)), name_(name) {}

    Name(const Library* library, SourceLocation name)
        : Name(library, {}, name) {}

    Name(Name&&) = default;
    Name& operator=(Name&&) = default;

    const Library* library() const { return library_; }
    const std::vector<SourceLocation>& nested_decls() const { return nested_decls_; }
    SourceLocation name() const { return name_; }

    bool operator==(const Name& other) const {
        if (LibraryName(library_) != LibraryName(other.library_)) {
            return false;
        }
        if (nested_decls_.size() != other.nested_decls_.size()) {
            return false;
        }
        for (size_t idx = 0u; idx < nested_decls_.size(); ++idx) {
            if (nested_decls_[idx].data() != other.nested_decls_[idx].data()) {
                return false;
            }
        }
        return name_.data() == other.name_.data();
    }
    bool operator!=(const Name& other) const {
        return name_.data() != other.name_.data();
    }

    bool operator<(const Name& other) const {
        if (LibraryName(library_) != LibraryName(other.library_)) {
            return LibraryName(library_) < LibraryName(other.library_);
        }
        if (nested_decls_.size() != other.nested_decls_.size()) {
            return nested_decls_.size() < other.nested_decls_.size();
        }
        for (size_t idx = 0u; idx < nested_decls_.size(); ++idx) {
            if (nested_decls_[idx].data() != other.nested_decls_[idx].data()) {
                return nested_decls_[idx].data() < other.nested_decls_[idx].data();
            }
        }
        return name_.data() < other.name_.data();
    }

private:
    const Library* library_ = nullptr;
    std::vector<SourceLocation> nested_decls_;
    SourceLocation name_;
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

    Decl(Kind kind, std::unique_ptr<raw::AttributeList> attributes, Name name)
        : kind(kind), attributes(std::move(attributes)), name(std::move(name)) {}

    Decl(Decl&&) = default;
    Decl& operator=(Decl&&) = default;

    const Kind kind;
    std::unique_ptr<raw::AttributeList> attributes;
    const Name name;
};

struct Type {
    virtual ~Type() {}

    enum struct Kind {
        Array,
        Vector,
        String,
        Handle,
        RequestHandle,
        Primitive,
        Identifier,
    };

    explicit Type(Kind kind, uint32_t size)
        : kind(kind), size(size) {}

    const Kind kind;
    // Set at construction time for most Types. Identifier types get
    // this set later, during compilation.
    uint32_t size;

    bool operator<(const Type& other) const;
};

struct ArrayType : public Type {
    ArrayType(uint32_t size, std::unique_ptr<Type> element_type, Size element_count)
        : Type(Kind::Array, size),
          element_type(std::move(element_type)),
          element_count(std::move(element_count)) {}

    std::unique_ptr<Type> element_type;
    Size element_count;

    bool operator<(const ArrayType& other) const {
        if (element_count.Value() != other.element_count.Value())
            return element_count.Value() < other.element_count.Value();
        return *element_type < *other.element_type;
    }
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

    bool operator<(const VectorType& other) const {
        if (element_count.Value() != other.element_count.Value())
            return element_count.Value() < other.element_count.Value();
        if (nullability != other.nullability)
            return nullability < other.nullability;
        return *element_type < *other.element_type;
    }
};

struct StringType : public Type {
    StringType(Size max_size, types::Nullability nullability)
        : Type(Kind::String, 16u), max_size(std::move(max_size)),
          nullability(nullability) {}

    Size max_size;
    types::Nullability nullability;

    bool operator<(const StringType& other) const {
        if (max_size.Value() != other.max_size.Value())
            return max_size.Value() < other.max_size.Value();
        return nullability < other.nullability;
    }
};

struct HandleType : public Type {
    HandleType(types::HandleSubtype subtype, types::Nullability nullability)
        : Type(Kind::Handle, 4u), subtype(subtype), nullability(nullability) {}

    types::HandleSubtype subtype;
    types::Nullability nullability;

    bool operator<(const HandleType& other) const {
        if (subtype != other.subtype)
            return subtype < other.subtype;
        return nullability < other.nullability;
    }
};

struct RequestHandleType : public Type {
    RequestHandleType(Name name, types::Nullability nullability)
        : Type(Kind::RequestHandle, 4u), name(std::move(name)), nullability(nullability) {}

    Name name;
    types::Nullability nullability;

    bool operator<(const RequestHandleType& other) const {
        if (name != other.name)
            return name < other.name;
        return nullability < other.nullability;
    }
};

struct PrimitiveType : public Type {
    static uint32_t SubtypeSize(types::PrimitiveSubtype subtype) {
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
        : Type(Kind::Primitive, SubtypeSize(subtype)), subtype(subtype) {}

    types::PrimitiveSubtype subtype;

    bool operator<(const PrimitiveType& other) const {
        return subtype < other.subtype;
    }
};

struct IdentifierType : public Type {
    IdentifierType(Name name, types::Nullability nullability)
        : Type(Kind::Identifier, 0u), name(std::move(name)), nullability(nullability) {}

    Name name;
    types::Nullability nullability;

    bool operator<(const IdentifierType& other) const {
        if (name != other.name)
            return name < other.name;
        return nullability < other.nullability;
    }
};

inline bool Type::operator<(const Type& other) const {
    if (kind != other.kind)
        return kind < other.kind;
    switch (kind) {
    case Type::Kind::Array: {
        auto left_array = static_cast<const ArrayType*>(this);
        auto right_array = static_cast<const ArrayType*>(&other);
        return *left_array < *right_array;
    }
    case Type::Kind::Vector: {
        auto left_vector = static_cast<const VectorType*>(this);
        auto right_vector = static_cast<const VectorType*>(&other);
        return *left_vector < *right_vector;
    }
    case Type::Kind::String: {
        auto left_string = static_cast<const StringType*>(this);
        auto right_string = static_cast<const StringType*>(&other);
        return *left_string < *right_string;
    }
    case Type::Kind::Handle: {
        auto left_handle = static_cast<const HandleType*>(this);
        auto right_handle = static_cast<const HandleType*>(&other);
        return *left_handle < *right_handle;
    }
    case Type::Kind::RequestHandle: {
        auto left_request = static_cast<const RequestHandleType*>(this);
        auto right_request = static_cast<const RequestHandleType*>(&other);
        return *left_request < *right_request;
    }
    case Type::Kind::Primitive: {
        auto left_primitive = static_cast<const PrimitiveType*>(this);
        auto right_primitive = static_cast<const PrimitiveType*>(&other);
        return *left_primitive < *right_primitive;
    }
    case Type::Kind::Identifier: {
        auto left_identifier = static_cast<const IdentifierType*>(this);
        auto right_identifier = static_cast<const IdentifierType*>(&other);
        return *left_identifier < *right_identifier;
    }
    }
}

struct Const : public Decl {
    Const(std::unique_ptr<raw::AttributeList> attributes, Name name, std::unique_ptr<Type> type, std::unique_ptr<raw::Constant> value)
        : Decl(Kind::kConst, std::move(attributes), std::move(name)), type(std::move(type)), value(std::move(value)) {}
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

    Enum(std::unique_ptr<raw::AttributeList> attributes, Name name, types::PrimitiveSubtype type, std::vector<Member> members)
        : Decl(Kind::kEnum, std::move(attributes), std::move(name)), type(type), members(std::move(members)) {}

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

    Interface(std::unique_ptr<raw::AttributeList> attributes, Name name, std::vector<Method> methods)
        : Decl(Kind::kInterface, std::move(attributes), std::move(name)), methods(std::move(methods)) {}

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

    Struct(std::unique_ptr<raw::AttributeList> attributes, Name name, std::vector<Member> members)
        : Decl(Kind::kStruct, std::move(attributes), std::move(name)), members(std::move(members)) {}

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

    Union(std::unique_ptr<raw::AttributeList> attributes, Name name, std::vector<Member> members)
        : Decl(Kind::kUnion, std::move(attributes), std::move(name)), members(std::move(members)) {}

    std::vector<Member> members;
    // The offset of each of the union members is the same, so store
    // it here as well.
    FieldShape fieldshape;
};

class Library {
public:
    Library(const std::map<StringView, std::unique_ptr<Library>>* dependencies,
            ErrorReporter* error_reporter);

    bool ConsumeFile(std::unique_ptr<raw::File> file);
    bool Compile();

    StringView name() const {
        return library_name_.data();
    }

private:
    bool Fail(StringView message);
    bool Fail(const SourceLocation& location, StringView message);
    bool Fail(const Name& name, StringView message) {
      return Fail(name.name(), message);
    }
    bool Fail(const Decl& decl, StringView message) {
      return Fail(decl.name, message);
    }

    bool CompileCompoundIdentifier(const raw::CompoundIdentifier* compound_identifier,
                                   Name* name_out);

    bool ParseSize(std::unique_ptr<raw::Constant> raw_constant, Size* out_size);

    bool RegisterDecl(Decl* decl);

    bool ConsumeType(std::unique_ptr<raw::Type> type, std::unique_ptr<Type>* out_type, const SourceLocation& source_location);

    bool ConsumeConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const_declaration);
    bool ConsumeEnumDeclaration(std::unique_ptr<raw::EnumDeclaration> enum_declaration);
    bool ConsumeInterfaceDeclaration(std::unique_ptr<raw::InterfaceDeclaration> interface_declaration);
    bool ConsumeStructDeclaration(std::unique_ptr<raw::StructDeclaration> struct_declaration);
    bool ConsumeUnionDeclaration(std::unique_ptr<raw::UnionDeclaration> union_declaration);

    // Returns nullptr when |type| does not correspond directly to a
    // declaration. For example, if |type| refers to int32 or if it is
    // a struct pointer, this will return null. If it is a struct, it
    // will return a pointer to the declaration of the type.
    Decl* LookupType(const flat::Type* type) const;

    // Returns nullptr when the |name| cannot be resolved to a
    // Name. Otherwise it returns the declaration.
    Decl* LookupType(const Name& name) const;

    std::set<Decl*> DeclDependencies(Decl* decl);

    bool SortDeclarations();

    bool CompileConst(Const* const_declaration);
    bool CompileEnum(Enum* enum_declaration);
    bool CompileInterface(Interface* interface_declaration);
    bool CompileStruct(Struct* struct_declaration);
    bool CompileUnion(Union* union_declaration);

    bool CompileArrayType(ArrayType* array_type, TypeShape* out_type_metadata);
    bool CompileVectorType(VectorType* vector_type, TypeShape* out_type_metadata);
    bool CompileStringType(StringType* string_type, TypeShape* out_type_metadata);
    bool CompileHandleType(HandleType* handle_type, TypeShape* out_type_metadata);
    bool CompileRequestHandleType(RequestHandleType* request_type, TypeShape* out_type_metadata);
    bool CompilePrimitiveType(PrimitiveType* primitive_type, TypeShape* out_type_metadata);
    bool CompileIdentifierType(IdentifierType* identifier_type, TypeShape* out_type_metadata);
    bool CompileType(Type* type, TypeShape* out_type_metadata);

public:
    // TODO(TO-702) Add a validate literal function. Some things
    // (e.g. array indexes) want to check the value but print the
    // constant, say.
    template <typename IntType>
    bool ParseIntegerLiteral(const raw::NumericLiteral* literal, IntType* out_value) const {
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
    bool ParseIntegerConstant(const raw::Constant* constant, IntType* out_value) const {
        if (!constant) {
            return false;
        }
        switch (constant->kind) {
        case raw::Constant::Kind::Identifier: {
            auto identifier_constant = static_cast<const raw::IdentifierConstant*>(constant);
            auto identifier = identifier_constant->identifier.get();
            // TODO(TO-701) Support more parts of names.
            Name name(this, identifier->components[0]->location);
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

    const std::map<StringView, std::unique_ptr<Library>>* dependencies_;

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
    std::map<const Name*, Decl*, PtrCompare<Name>> declarations_;

    ErrorReporter* error_reporter_;
};

} // namespace flat
} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FLAT_AST_H_
