// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FLAT_AST_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FLAT_AST_H_

#include <assert.h>
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
    bool operator!=(const Name& other) const { return !operator==(other); }

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
        kTable,
        kUnion,
    };

    Decl(Kind kind, std::unique_ptr<raw::AttributeList> attributes, Name name)
        : kind(kind), attributes(std::move(attributes)), name(std::move(name)) {}

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

    explicit Type(Kind kind, uint32_t size, types::Nullability nullability)
        : kind(kind), size(size), nullability(nullability) {}

    const Kind kind;
    // Set at construction time for most Types. Identifier types get
    // this set later, during compilation.
    uint32_t size;
    const types::Nullability nullability;

    // Comparison helper object.
    class Comparison {
    public:
        Comparison() = default;
        template <class T>
        Comparison Compare(const T& a, const T& b) const {
            if (result_ != 0)
                return Comparison(result_);
            if (a < b)
                return Comparison(-1);
            if (b < a)
                return Comparison(1);
            return Comparison(0);
        }

        bool IsLessThan() const {
            return result_ < 0;
        }

    private:
        Comparison(int result)
            : result_(result) {}
        const int result_ = 0;
    };

    bool operator<(const Type& other) const {
        if (kind != other.kind)
            return kind < other.kind;
        return Compare(other).IsLessThan();
    }

    // Compare this object against 'other'.
    // It's guaranteed that this->kind == other.kind.
    // Return <0 if *this < other, ==0 if *this == other, and >0 if *this > other.
    // Derived types should override this, but also call this implementation.
    virtual Comparison Compare(const Type& other) const {
        assert(kind == other.kind);
        return Comparison()
            .Compare(nullability, other.nullability);
    }
};

struct ArrayType : public Type {
    ArrayType(std::unique_ptr<Type> element_type, Size element_count)
        : Type(Kind::kArray, 0u, types::Nullability::kNonnullable),
          element_type(std::move(element_type)),
          element_count(std::move(element_count)) {}

    std::unique_ptr<Type> element_type;
    Size element_count;

    Comparison Compare(const Type& other) const override {
        const auto& o = static_cast<const ArrayType&>(other);
        return Type::Compare(o)
            .Compare(element_count.Value(), o.element_count.Value())
            .Compare(*element_type, *o.element_type);
    }
};

struct VectorType : public Type {
    VectorType(std::unique_ptr<Type> element_type, Size element_count,
               types::Nullability nullability)
        : Type(Kind::kVector, 16u, nullability), element_type(std::move(element_type)),
          element_count(std::move(element_count)) {}

    std::unique_ptr<Type> element_type;
    Size element_count;

    Comparison Compare(const Type& other) const override {
        const auto& o = static_cast<const VectorType&>(other);
        return Type::Compare(o)
            .Compare(element_count.Value(), o.element_count.Value())
            .Compare(*element_type, *o.element_type);
    }
};

struct StringType : public Type {
    StringType(Size max_size, types::Nullability nullability)
        : Type(Kind::kString, 16u, nullability), max_size(std::move(max_size)) {}

    Size max_size;

    Comparison Compare(const Type& other) const override {
        const auto& o = static_cast<const StringType&>(other);
        return Type::Compare(o)
            .Compare(max_size.Value(), o.max_size.Value());
    }
};

struct HandleType : public Type {
    HandleType(types::HandleSubtype subtype, types::Nullability nullability)
        : Type(Kind::kHandle, 4u, nullability), subtype(subtype) {}

    const types::HandleSubtype subtype;

    Comparison Compare(const Type& other) const override {
        const auto& o = *static_cast<const HandleType*>(&other);
        return Type::Compare(o)
            .Compare(subtype, o.subtype);
    }
};

struct RequestHandleType : public Type {
    RequestHandleType(Name name, types::Nullability nullability)
        : Type(Kind::kRequestHandle, 4u, nullability), name(std::move(name)) {}

    Name name;

    Comparison Compare(const Type& other) const override {
        const auto& o = static_cast<const RequestHandleType&>(other);
        return Type::Compare(o)
            .Compare(name, o.name);
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
        : Type(Kind::kPrimitive, SubtypeSize(subtype), types::Nullability::kNonnullable),
          subtype(subtype) {}

    types::PrimitiveSubtype subtype;

    Comparison Compare(const Type& other) const override {
        const auto& o = static_cast<const PrimitiveType&>(other);
        return Type::Compare(o)
            .Compare(subtype, o.subtype);
    }
};

struct IdentifierType : public Type {
    IdentifierType(Name name, types::Nullability nullability)
        : Type(Kind::kIdentifier, 0u, nullability), name(std::move(name)) {}

    Name name;

    Comparison Compare(const Type& other) const override {
        const auto& o = static_cast<const RequestHandleType&>(other);
        return Type::Compare(o)
            .Compare(name, o.name);
    }
};

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
        Member(SourceLocation name, std::unique_ptr<Constant> value, std::unique_ptr<raw::AttributeList> attributes)
            : name(name), value(std::move(value)), attributes(std::move(attributes)) {}
        SourceLocation name;
        std::unique_ptr<Constant> value;
        std::unique_ptr<raw::AttributeList> attributes;
    };

    Enum(std::unique_ptr<raw::AttributeList> attributes, Name name, types::PrimitiveSubtype subtype,
         std::vector<Member> members)
        : Decl(Kind::kEnum, std::move(attributes), std::move(name)),
          type(std::make_unique<PrimitiveType>(subtype)),
          members(std::move(members)) {}

    std::unique_ptr<PrimitiveType> type;
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
               std::unique_ptr<raw::Ordinal> ordinal, SourceLocation name,
               std::unique_ptr<Message> maybe_request,
               std::unique_ptr<Message> maybe_response)
            : attributes(std::move(attributes)), ordinal(std::move(ordinal)), name(std::move(name)),
              maybe_request(std::move(maybe_request)), maybe_response(std::move(maybe_response)) {
            assert(this->maybe_request != nullptr || this->maybe_response != nullptr);
        }

        std::unique_ptr<raw::AttributeList> attributes;
        std::unique_ptr<raw::Ordinal> ordinal;
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
    // Pointers here are set after superinterfaces are compiled, and
    // are owned by the correspending superinterface.
    std::vector<const Method*> all_methods;
};

struct Struct : public Decl {
    struct Member {
        Member(std::unique_ptr<Type> type, SourceLocation name,
               std::unique_ptr<Constant> maybe_default_value,
               std::unique_ptr<raw::AttributeList> attributes)
            : type(std::move(type)), name(std::move(name)),
              maybe_default_value(std::move(maybe_default_value)),
              attributes(std::move(attributes)) {}
        std::unique_ptr<Type> type;
        SourceLocation name;
        std::unique_ptr<Constant> maybe_default_value;
        std::unique_ptr<raw::AttributeList> attributes;
        FieldShape fieldshape;
    };

    Struct(std::unique_ptr<raw::AttributeList> attributes, Name name, std::vector<Member> members)
        : Decl(Kind::kStruct, std::move(attributes), std::move(name)), members(std::move(members)) {
    }

    std::vector<Member> members;
    TypeShape typeshape;
    bool recursive = false;
};

struct Table : public Decl {
    struct Member {
        Member(std::unique_ptr<raw::Ordinal> ordinal, std::unique_ptr<Type> type, SourceLocation name,
               std::unique_ptr<Constant> maybe_default_value,
               std::unique_ptr<raw::AttributeList> attributes)
            : ordinal(std::move(ordinal)),
              maybe_used(std::make_unique<Used>(std::move(type), std::move(name),
                                                std::move(maybe_default_value),
                                                std::move(attributes))) {}
        Member(std::unique_ptr<raw::Ordinal> ordinal)
            : ordinal(std::move(ordinal)) {}
        std::unique_ptr<raw::Ordinal> ordinal;
        struct Used {
            Used(std::unique_ptr<Type> type, SourceLocation name,
                 std::unique_ptr<Constant> maybe_default_value,
                 std::unique_ptr<raw::AttributeList> attributes)
                : type(std::move(type)), name(std::move(name)),
                  maybe_default_value(std::move(maybe_default_value)),
                  attributes(std::move(attributes)) {}
            std::unique_ptr<Type> type;
            SourceLocation name;
            std::unique_ptr<Constant> maybe_default_value;
            std::unique_ptr<raw::AttributeList> attributes;
            TypeShape typeshape;
        };
        std::unique_ptr<Used> maybe_used;
    };

    Table(std::unique_ptr<raw::AttributeList> attributes, Name name, std::vector<Member> members)
        : Decl(Kind::kTable, std::move(attributes), std::move(name)), members(std::move(members)) {
    }

    std::vector<Member> members;
    TypeShape typeshape;
    bool recursive = false;
};

struct Union : public Decl {
    struct Member {
        Member(std::unique_ptr<Type> type, SourceLocation name, std::unique_ptr<raw::AttributeList> attributes)
            : type(std::move(type)), name(std::move(name)), attributes(std::move(attributes)) {}
        std::unique_ptr<Type> type;
        SourceLocation name;
        std::unique_ptr<raw::AttributeList> attributes;
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

class Libraries {
public:
    // Insert |library|.
    bool Insert(std::unique_ptr<Library> library);

    // Lookup a library by its |library_name|.
    bool Lookup(const std::vector<StringView>& library_name,
                Library** out_library) const;

private:
    std::map<std::vector<StringView>, std::unique_ptr<Library>> all_libraries_;
};

class Dependencies {
public:
    // Register a dependency to a library. The newly recorded dependent library
    // will be referenced by its name, and may also be optionally be referenced
    // by an alias.
    bool Register(StringView filename, Library* dep_library,
                  const std::unique_ptr<raw::Identifier>& maybe_alias);

    // Lookup a dependent library by |filename| and |name|.
    bool Lookup(StringView filename, const std::vector<StringView>& name,
                Library** out_library);

    const std::set<Library*>& dependencies() const { return dependencies_aggregate_; };

private:
    bool InsertByName(StringView filename, const std::vector<StringView>& name,
                      Library* library);

    typedef std::map<std::vector<StringView>, Library*> ByName;
    typedef std::map<std::string, std::unique_ptr<ByName>> ByFilename;

    ByFilename dependencies_;
    std::set<Library*> dependencies_aggregate_;
};

class Library {
public:
    Library(const Libraries* all_libraries, ErrorReporter* error_reporter)
        : all_libraries_(all_libraries), error_reporter_(error_reporter) {}

    bool ConsumeFile(std::unique_ptr<raw::File> file);
    bool Compile();

    const std::vector<StringView>& name() const { return library_name_; }
    const std::vector<std::string>& errors() const { return error_reporter_->errors(); }

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
    bool ConsumeTypeAlias(std::unique_ptr<raw::Using> using_directive);
    bool ConsumeConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const_declaration);
    bool ConsumeEnumDeclaration(std::unique_ptr<raw::EnumDeclaration> enum_declaration);
    bool
    ConsumeInterfaceDeclaration(std::unique_ptr<raw::InterfaceDeclaration> interface_declaration);
    bool ConsumeStructDeclaration(std::unique_ptr<raw::StructDeclaration> struct_declaration);
    bool ConsumeTableDeclaration(std::unique_ptr<raw::TableDeclaration> table_declaration);
    bool ConsumeUnionDeclaration(std::unique_ptr<raw::UnionDeclaration> union_declaration);

    bool TypeCanBeConst(const Type* type);
    bool TypeInferConstantType(const Constant* constant,
                               std::unique_ptr<Type>* out_type);
    const Type* TypeResolve(const Type* type);
    bool TypeIsConvertibleTo(const Type* from_type, const Type* to_type);

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
    bool CompileTable(Table* table_declaration);
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
        auto data = literal->location().data();
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

    const std::set<Library*>& dependencies() const;

    std::vector<StringView> library_name_;

    std::vector<std::unique_ptr<Using>> using_;
    std::vector<std::unique_ptr<Const>> const_declarations_;
    std::vector<std::unique_ptr<Enum>> enum_declarations_;
    std::vector<std::unique_ptr<Interface>> interface_declarations_;
    std::vector<std::unique_ptr<Struct>> struct_declarations_;
    std::vector<std::unique_ptr<Table>> table_declarations_;
    std::vector<std::unique_ptr<Union>> union_declarations_;

    // All Decl pointers here are non-null and are owned by the
    // various foo_declarations_.
    std::vector<Decl*> declaration_order_;

private:
    std::unique_ptr<raw::AttributeList> attributes_;

    Dependencies dependencies_;
    const Libraries* all_libraries_;

    // All Name, Constant, Using, and Decl pointers here are non-null and are
    // owned by the various foo_declarations_.
    std::map<const Name*, Using*, PtrCompare<Name>> type_aliases_;
    std::map<const Name*, Decl*, PtrCompare<Name>> declarations_;
    std::map<const Name*, Const*, PtrCompare<Name>> constants_;

    ErrorReporter* error_reporter_;
};

} // namespace flat
} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FLAT_AST_H_
