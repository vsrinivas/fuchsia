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
    bool operator()(const T* left, const T* right) const { return *left < *right; }
};

struct Decl;
class Library;

// This is needed (for now) to work around declaration order issues.
std::string LibraryName(const Library* library, StringView separator);

struct Name {
    Name()
        : name_(SourceLocation()) {}

    Name(const Library* library, SourceLocation name)
        : library_(library), name_(name) {}

    Name(Name&&) = default;
    Name& operator=(Name&&) = default;

    const Library* library() const { return library_; }
    SourceLocation name() const { return name_; }

    bool operator==(const Name& other) const {
        if (LibraryName(library_, ".") != LibraryName(other.library_, ".")) {
            return false;
        }
        return name_.data() == other.name_.data();
    }
    bool operator!=(const Name& other) const { return name_.data() != other.name_.data(); }

    bool operator<(const Name& other) const {
        if (LibraryName(library_, ".") != LibraryName(other.library_, ".")) {
            return LibraryName(library_, ".") < LibraryName(other.library_, ".");
        }
        return name_.data() < other.name_.data();
    }

private:
    const Library* library_ = nullptr;
    SourceLocation name_;
};

struct Constant {
    virtual ~Constant() {}

    enum struct Kind {
        kIdentifier,
        kLiteral,
    };

    explicit Constant(Kind kind)
        : kind(kind) {}

    const Kind kind;
};

struct IdentifierConstant : Constant {
    explicit IdentifierConstant(Name name)
        : Constant(Kind::kIdentifier), name(std::move(name)) {}

    Name name;
};

struct LiteralConstant : Constant {
    explicit LiteralConstant(std::unique_ptr<raw::Literal> literal)
        : Constant(Kind::kLiteral), literal(std::move(literal)) {}

    std::unique_ptr<raw::Literal> literal;
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
    IntConstant(std::unique_ptr<Constant> constant, IntType value)
        : constant_(std::move(constant)), value_(value) {}

    explicit IntConstant(IntType value)
        : value_(value) {}

    IntConstant()
        : value_(0) {}

    IntType Value() const { return value_; }

    static IntConstant Max() { return IntConstant(std::numeric_limits<IntType>::max()); }

private:
    std::unique_ptr<Constant> constant_;
    IntType value_;
};

using Size = IntConstant<uint32_t>;

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

    bool HasAttribute(fidl::StringView name) const;
    fidl::StringView GetAttribute(fidl::StringView name) const;
    std::string GetName() const;

    bool compiling = false;
    bool compiled = false;
};

struct Type {
    virtual ~Type() {}

    enum struct Kind {
        kArray,
        kVector,
        kString,
        kHandle,
        kRequestHandle,
        kPrimitive,
        kIdentifier,
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
        : Type(Kind::kArray, size), element_type(std::move(element_type)),
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
        : Type(Kind::kVector, 16u), element_type(std::move(element_type)),
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
        : Type(Kind::kString, 16u), max_size(std::move(max_size)), nullability(nullability) {}

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
        : Type(Kind::kHandle, 4u), subtype(subtype), nullability(nullability) {}

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
        : Type(Kind::kRequestHandle, 4u), name(std::move(name)), nullability(nullability) {}

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
        case types::PrimitiveSubtype::kBool:
        case types::PrimitiveSubtype::kInt8:
        case types::PrimitiveSubtype::kUint8:
            return 1u;

        case types::PrimitiveSubtype::kInt16:
        case types::PrimitiveSubtype::kUint16:
            return 2u;

        case types::PrimitiveSubtype::kFloat32:
        case types::PrimitiveSubtype::kInt32:
        case types::PrimitiveSubtype::kUint32:
            return 4u;

        case types::PrimitiveSubtype::kFloat64:
        case types::PrimitiveSubtype::kInt64:
        case types::PrimitiveSubtype::kUint64:
            return 8u;
        }
    }

    explicit PrimitiveType(types::PrimitiveSubtype subtype)
        : Type(Kind::kPrimitive, SubtypeSize(subtype)), subtype(subtype) {}

    types::PrimitiveSubtype subtype;

    bool operator<(const PrimitiveType& other) const { return subtype < other.subtype; }
};

struct IdentifierType : public Type {
    IdentifierType(Name name, types::Nullability nullability)
        : Type(Kind::kIdentifier, 0u), name(std::move(name)), nullability(nullability) {}

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
    case Type::Kind::kArray: {
        auto left_array = static_cast<const ArrayType*>(this);
        auto right_array = static_cast<const ArrayType*>(&other);
        return *left_array < *right_array;
    }
    case Type::Kind::kVector: {
        auto left_vector = static_cast<const VectorType*>(this);
        auto right_vector = static_cast<const VectorType*>(&other);
        return *left_vector < *right_vector;
    }
    case Type::Kind::kString: {
        auto left_string = static_cast<const StringType*>(this);
        auto right_string = static_cast<const StringType*>(&other);
        return *left_string < *right_string;
    }
    case Type::Kind::kHandle: {
        auto left_handle = static_cast<const HandleType*>(this);
        auto right_handle = static_cast<const HandleType*>(&other);
        return *left_handle < *right_handle;
    }
    case Type::Kind::kRequestHandle: {
        auto left_request = static_cast<const RequestHandleType*>(this);
        auto right_request = static_cast<const RequestHandleType*>(&other);
        return *left_request < *right_request;
    }
    case Type::Kind::kPrimitive: {
        auto left_primitive = static_cast<const PrimitiveType*>(this);
        auto right_primitive = static_cast<const PrimitiveType*>(&other);
        return *left_primitive < *right_primitive;
    }
    case Type::Kind::kIdentifier: {
        auto left_identifier = static_cast<const IdentifierType*>(this);
        auto right_identifier = static_cast<const IdentifierType*>(&other);
        return *left_identifier < *right_identifier;
    }
    }
}

struct Using {
    Using(Name name, std::unique_ptr<PrimitiveType> type)
        : name(std::move(name)), type(std::move(type)) {}

    const Name name;
    const std::unique_ptr<PrimitiveType> type;
};

struct Const : public Decl {
    Const(std::unique_ptr<raw::AttributeList> attributes, Name name, std::unique_ptr<Type> type,
          std::unique_ptr<Constant> value)
        : Decl(Kind::kConst, std::move(attributes), std::move(name)), type(std::move(type)),
          value(std::move(value)) {}
    std::unique_ptr<Type> type;
    std::unique_ptr<Constant> value;
};

struct Enum : public Decl {
    struct Member {
        Member(SourceLocation name, std::unique_ptr<Constant> value)
            : name(name), value(std::move(value)) {}
        SourceLocation name;
        std::unique_ptr<Constant> value;
    };

    Enum(std::unique_ptr<raw::AttributeList> attributes, Name name, types::PrimitiveSubtype type,
         std::vector<Member> members)
        : Decl(Kind::kEnum, std::move(attributes), std::move(name)), type(type),
          members(std::move(members)) {}

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
            FieldShape fieldshape;

            // A simple parameter is one that is easily represented in C.
            // Specifically, the parameter is either a string with a max length
            // or does not reference any secondary objects,
            bool IsSimple() const;
        };

        struct Message {
            std::vector<Parameter> parameters;
            TypeShape typeshape;
        };

        Method(Method&&) = default;
        Method& operator=(Method&&) = default;

        Method(std::unique_ptr<raw::AttributeList> attributes,
               Ordinal ordinal, SourceLocation name, std::unique_ptr<Message> maybe_request,
               std::unique_ptr<Message> maybe_response)
            : attributes(std::move(attributes)), ordinal(std::move(ordinal)), name(std::move(name)),
              maybe_request(std::move(maybe_request)), maybe_response(std::move(maybe_response)) {
            assert(this->maybe_request != nullptr || this->maybe_response != nullptr);
        }

        std::unique_ptr<raw::AttributeList> attributes;
        Ordinal ordinal;
        SourceLocation name;
        std::unique_ptr<Message> maybe_request;
        std::unique_ptr<Message> maybe_response;
    };

    Interface(std::unique_ptr<raw::AttributeList> attributes, Name name,
              std::vector<Name> superinterfaces, std::vector<Method> methods)
        : Decl(Kind::kInterface, std::move(attributes), std::move(name)),
          superinterfaces(std::move(superinterfaces)), methods(std::move(methods)) {}

    std::vector<Name> superinterfaces;
    std::vector<Method> methods;
};

struct Struct : public Decl {
    struct Member {
        Member(std::unique_ptr<Type> type, SourceLocation name,
               std::unique_ptr<Constant> maybe_default_value)
            : type(std::move(type)), name(std::move(name)),
              maybe_default_value(std::move(maybe_default_value)) {}
        std::unique_ptr<Type> type;
        SourceLocation name;
        std::unique_ptr<Constant> maybe_default_value;
        FieldShape fieldshape;
    };

    Struct(std::unique_ptr<raw::AttributeList> attributes, Name name, std::vector<Member> members)
        : Decl(Kind::kStruct, std::move(attributes), std::move(name)), members(std::move(members)) {
    }

    std::vector<Member> members;
    TypeShape typeshape;
    bool recursive = false;
};

struct Union : public Decl {
    struct Member {
        Member(std::unique_ptr<Type> type, SourceLocation name)
            : type(std::move(type)), name(std::move(name)) {}
        std::unique_ptr<Type> type;
        SourceLocation name;
        FieldShape fieldshape;
    };

    Union(std::unique_ptr<raw::AttributeList> attributes, Name name, std::vector<Member> members)
        : Decl(Kind::kUnion, std::move(attributes), std::move(name)), members(std::move(members)) {}

    std::vector<Member> members;
    TypeShape typeshape;
    // The offset of each of the union members is the same, so store
    // it here as well.
    FieldShape membershape;
    bool recursive = false;
};

class Library {
public:
    Library(const std::map<std::vector<StringView>, std::unique_ptr<Library>>* dependencies,
            ErrorReporter* error_reporter);

    bool ConsumeFile(std::unique_ptr<raw::File> file);
    bool Compile();

    const std::vector<StringView>& name() const { return library_name_; }

private:
    bool Fail(StringView message);
    bool Fail(const SourceLocation& location, StringView message);
    bool Fail(const Name& name, StringView message) { return Fail(name.name(), message); }
    bool Fail(const Decl& decl, StringView message) { return Fail(decl.name, message); }

    bool CompileCompoundIdentifier(const raw::CompoundIdentifier* compound_identifier,
                                   SourceLocation location, Name* out_name);

    bool ParseSize(std::unique_ptr<Constant> constant, Size* out_size);

    void RegisterConst(Const* decl);
    bool RegisterDecl(Decl* decl);

    bool ConsumeConstant(std::unique_ptr<raw::Constant> raw_constant, SourceLocation location,
                         std::unique_ptr<Constant>* out_constant);
    bool ConsumeType(std::unique_ptr<raw::Type> raw_type, SourceLocation location,
                     std::unique_ptr<Type>* out_type);

    bool ConsumeUsing(std::unique_ptr<raw::Using> using_directive);
    bool ConsumeConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const_declaration);
    bool ConsumeEnumDeclaration(std::unique_ptr<raw::EnumDeclaration> enum_declaration);
    bool
    ConsumeInterfaceDeclaration(std::unique_ptr<raw::InterfaceDeclaration> interface_declaration);
    bool ConsumeStructDeclaration(std::unique_ptr<raw::StructDeclaration> struct_declaration);
    bool ConsumeUnionDeclaration(std::unique_ptr<raw::UnionDeclaration> union_declaration);

    bool TypecheckString(const IdentifierConstant* identifier);
    bool TypecheckPrimitive(const IdentifierConstant* identifier);
    bool TypecheckConst(const Const* const_declaration);

    // Given a const declaration of the form
    //     const type foo = name;
    // return the declaration corresponding to name.
    Decl* LookupConstant(const Type* type, const Name& name);

    // Given a name, checks whether that name corresponds to a type alias. If
    // so, returns the type. Otherwise, returns nullptr.
    PrimitiveType* LookupTypeAlias(const Name& name) const;

    // Returns nullptr when |type| does not correspond directly to a
    // declaration. For example, if |type| refers to int32 or if it is
    // a struct pointer, this will return null. If it is a struct, it
    // will return a pointer to the declaration of the type.
    enum class LookupOption {
        kIgnoreNullable,
        kIncludeNullable,
    };
    Decl* LookupDeclByType(const flat::Type* type, LookupOption option) const;

    bool DeclDependencies(Decl* decl, std::set<Decl*>* out_edges);

    bool SortDeclarations();

    bool CompileLibraryName();

    bool CompileConst(Const* const_declaration);
    bool CompileEnum(Enum* enum_declaration);
    bool CompileInterface(Interface* interface_declaration);
    bool CompileStruct(Struct* struct_declaration);
    bool CompileUnion(Union* union_declaration);

    // Compiling a type both validates the type, and computes shape
    // information for the type. In particular, we validate that
    // optional identifier types refer to things that can in fact be
    // nullable (ie not enums).
    bool CompileArrayType(ArrayType* array_type, TypeShape* out_type_metadata);
    bool CompileVectorType(VectorType* vector_type, TypeShape* out_type_metadata);
    bool CompileStringType(StringType* string_type, TypeShape* out_type_metadata);
    bool CompileHandleType(HandleType* handle_type, TypeShape* out_type_metadata);
    bool CompileRequestHandleType(RequestHandleType* request_type, TypeShape* out_type_metadata);
    bool CompilePrimitiveType(PrimitiveType* primitive_type, TypeShape* out_type_metadata);
    bool CompileIdentifierType(IdentifierType* identifier_type, TypeShape* out_type_metadata);
    bool CompileType(Type* type, TypeShape* out_type_metadata);

public:
    // Returns nullptr when the |name| cannot be resolved to a
    // Name. Otherwise it returns the declaration.
    Decl* LookupDeclByName(const Name& name) const;

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
    bool ParseIntegerConstant(const Constant* constant, IntType* out_value) const {
        if (!constant) {
            return false;
        }
        switch (constant->kind) {
        case Constant::Kind::kIdentifier: {
            auto identifier_constant = static_cast<const IdentifierConstant*>(constant);
            auto decl = LookupDeclByName(identifier_constant->name);
            if (!decl || decl->kind != Decl::Kind::kConst)
                return false;
            return ParseIntegerConstant(static_cast<Const*>(decl)->value.get(), out_value);
        }
        case Constant::Kind::kLiteral: {
            auto literal_constant = static_cast<const LiteralConstant*>(constant);
            switch (literal_constant->literal->kind) {
            case raw::Literal::Kind::kString:
            case raw::Literal::Kind::kTrue:
            case raw::Literal::Kind::kFalse: {
                return false;
            }

            case raw::Literal::Kind::kNumeric: {
                auto numeric_literal =
                    static_cast<const raw::NumericLiteral*>(literal_constant->literal.get());
                return ParseIntegerLiteral<IntType>(numeric_literal, out_value);
            }
            }
        }
        }
    }

    bool HasAttribute(fidl::StringView name) const;

    const std::map<std::vector<StringView>, std::unique_ptr<Library>>* dependencies_;

    std::vector<StringView> library_name_;

    std::vector<std::unique_ptr<Using>> using_;
    std::vector<std::unique_ptr<Const>> const_declarations_;
    std::vector<std::unique_ptr<Enum>> enum_declarations_;
    std::vector<std::unique_ptr<Interface>> interface_declarations_;
    std::vector<std::unique_ptr<Struct>> struct_declarations_;
    std::vector<std::unique_ptr<Union>> union_declarations_;

    // All Decl pointers here are non-null and are owned by the
    // various foo_declarations_.
    std::vector<Decl*> declaration_order_;

private:
    std::unique_ptr<raw::AttributeList> attributes_;

    // All Name, Constant, Using, and Decl pointers here are non-null and are
    // owned by the various foo_declarations_.
    std::map<const Name*, Using*, PtrCompare<Name>> type_aliases_;
    std::map<const Name*, Decl*, PtrCompare<Name>> declarations_;
    std::map<const Name*, Const*, PtrCompare<Name>> string_constants_;
    std::map<const Name*, Const*, PtrCompare<Name>> primitive_constants_;
    std::map<const Name*, Const*, PtrCompare<Name>> constants_;

    ErrorReporter* error_reporter_;
};

} // namespace flat
} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FLAT_AST_H_
