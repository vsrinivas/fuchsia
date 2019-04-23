// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FLAT_AST_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FLAT_AST_H_

#include <assert.h>
#include <stdint.h>

#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <type_traits>
#include <vector>

#include <lib/fit/function.h>

#include "attributes.h"
#include "error_reporter.h"
#include "raw_ast.h"
#include "type_shape.h"
#include "virtual_source_file.h"

// TODO(FIDL-487, ZX-3415): Decide if all cases of NumericConstantValue::Convert() are safe.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-compare"
#endif

namespace fidl {
namespace flat {

template <typename T>
struct PtrCompare {
    bool operator()(const T* left, const T* right) const { return *left < *right; }
};

class Typespace;
struct Decl;
class Library;

bool HasSimpleLayout(const Decl* decl);

// This is needed (for now) to work around declaration order issues.
std::string LibraryName(const Library* library, StringView separator);

// Name represents a scope name, i.e. a name within the context of a library
// or in the 'global' context. Names either reference (or name) things which
// appear in source, or are synthesized by the compiler (e.g. an anonymous
// struct name).
struct Name {
    Name() {}

    Name(const Library* library, const SourceLocation name)
        : library_(library),
          name_from_source_(std::make_unique<SourceLocation>(name)) {}

    Name(const Library* library, const std::string& name)
        : library_(library),
          anonymous_name_(std::make_unique<std::string>(name)) {}

    Name(Name&&) = default;
    Name& operator=(Name&&) = default;

    const Library* library() const { return library_; }
    const SourceLocation* maybe_location() const {
        return name_from_source_.get();
    }
    const StringView name_part() const {
        if (!name_from_source_)
            return *anonymous_name_.get();
        return name_from_source_->data();
    }

    bool operator==(const Name& other) const {
        // can't use the library name yet, not necesserily compiled!
        auto library_ptr = reinterpret_cast<uintptr_t>(library_);
        auto other_library_ptr = reinterpret_cast<uintptr_t>(other.library_);
        if (library_ptr != other_library_ptr)
            return false;
        return name_part() == other.name_part();
    }
    bool operator!=(const Name& other) const { return !operator==(other); }

    bool operator<(const Name& other) const {
        // can't use the library name yet, not necesserily compiled!
        auto library_ptr = reinterpret_cast<uintptr_t>(library_);
        auto other_library_ptr = reinterpret_cast<uintptr_t>(other.library_);
        if (library_ptr != other_library_ptr)
            return library_ptr < other_library_ptr;
        return name_part() < other.name_part();
    }

private:
    const Library* library_ = nullptr;
    std::unique_ptr<SourceLocation> name_from_source_;
    std::unique_ptr<std::string> anonymous_name_;
};

struct ConstantValue {
    virtual ~ConstantValue() {}

    enum struct Kind {
        kInt8,
        kInt16,
        kInt32,
        kInt64,
        kUint8,
        kUint16,
        kUint32,
        kUint64,
        kFloat32,
        kFloat64,
        kBool,
        kString,
    };

    virtual bool Convert(Kind kind, std::unique_ptr<ConstantValue>* out_value) const = 0;

    const Kind kind;

protected:
    explicit ConstantValue(Kind kind)
        : kind(kind) {}
};

template <typename ValueType>
struct NumericConstantValue : ConstantValue {
    static_assert(std::is_arithmetic<ValueType>::value && !std::is_same<ValueType, bool>::value,
                  "NumericConstantValue can only be used with a numeric ValueType!");

    NumericConstantValue(ValueType value)
        : ConstantValue(GetKind()), value(value) {}

    operator ValueType() const { return value; }

    friend bool operator==(const NumericConstantValue<ValueType>& l,
                           const NumericConstantValue<ValueType>& r) {
        return l.value == r.value;
    }

    friend bool operator<(const NumericConstantValue<ValueType>& l,
                          const NumericConstantValue<ValueType>& r) {
        return l.value < r.value;
    }

    friend bool operator>(const NumericConstantValue<ValueType>& l,
                          const NumericConstantValue<ValueType>& r) {
        return l.value > r.value;
    }

    friend bool operator!=(const NumericConstantValue<ValueType>& l,
                           const NumericConstantValue<ValueType>& r) {
        return l.value != r.value;
    }

    friend bool operator<=(const NumericConstantValue<ValueType>& l,
                           const NumericConstantValue<ValueType>& r) {
        return l.value <= r.value;
    }

    friend bool operator>=(const NumericConstantValue<ValueType>& l,
                           const NumericConstantValue<ValueType>& r) {
        return l.value >= r.value;
    }

    friend std::ostream& operator<<(std::ostream& os, const NumericConstantValue<ValueType>& v) {
        if constexpr (GetKind() == Kind::kInt8)
            os << static_cast<int>(v.value);
        else if constexpr (GetKind() == Kind::kUint8)
            os << static_cast<unsigned>(v.value);
        else
            os << v.value;
        return os;
    }

    virtual bool Convert(Kind kind, std::unique_ptr<ConstantValue>* out_value) const override {
        assert(out_value != nullptr);
        switch (kind) {
        case Kind::kInt8: {
            if (std::is_floating_point<ValueType>::value ||
                value < std::numeric_limits<int8_t>::lowest() ||
                value > std::numeric_limits<int8_t>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<int8_t>>(
                static_cast<int8_t>(value));
            return true;
        }
        case Kind::kInt16: {
            if (std::is_floating_point<ValueType>::value ||
                value < std::numeric_limits<int16_t>::lowest() ||
                value > std::numeric_limits<int16_t>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<int16_t>>(
                static_cast<int16_t>(value));
            return true;
        }
        case Kind::kInt32: {
            if (std::is_floating_point<ValueType>::value ||
                value < std::numeric_limits<int32_t>::lowest() ||
                value > std::numeric_limits<int32_t>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<int32_t>>(
                static_cast<int32_t>(value));
            return true;
        }
        case Kind::kInt64: {
            if (std::is_floating_point<ValueType>::value ||
                value < std::numeric_limits<int64_t>::lowest() ||
                value > std::numeric_limits<int64_t>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<int64_t>>(
                static_cast<int64_t>(value));
            return true;
        }
        case Kind::kUint8: {
            if (std::is_floating_point<ValueType>::value ||
                value < 0 || value > std::numeric_limits<uint8_t>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<uint8_t>>(
                static_cast<uint8_t>(value));
            return true;
        }
        case Kind::kUint16: {
            if (std::is_floating_point<ValueType>::value ||
                value < 0 || value > std::numeric_limits<uint16_t>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<uint16_t>>(
                static_cast<uint16_t>(value));
            return true;
        }
        case Kind::kUint32: {
            if (std::is_floating_point<ValueType>::value ||
                value < 0 || value > std::numeric_limits<uint32_t>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<uint32_t>>(
                static_cast<uint32_t>(value));
            return true;
        }
        case Kind::kUint64: {
            if (std::is_floating_point<ValueType>::value ||
                value < 0 || value > std::numeric_limits<uint64_t>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<uint64_t>>(
                static_cast<uint64_t>(value));
            return true;
        }
        case Kind::kFloat32: {
            if (!std::is_floating_point<ValueType>::value ||
                value < std::numeric_limits<float>::lowest() ||
                value > std::numeric_limits<float>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<float>>(static_cast<float>(value));
            return true;
        }
        case Kind::kFloat64: {
            if (!std::is_floating_point<ValueType>::value ||
                value < std::numeric_limits<double>::lowest() ||
                value > std::numeric_limits<double>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<double>>(static_cast<double>(value));
            return true;
        }
        case Kind::kString:
        case Kind::kBool:
            return false;
        }
    }

    static NumericConstantValue<ValueType> Min() {
        return NumericConstantValue<ValueType>(std::numeric_limits<ValueType>::lowest());
    }

    static NumericConstantValue<ValueType> Max() {
        return NumericConstantValue<ValueType>(std::numeric_limits<ValueType>::max());
    }

    ValueType value;

private:
    constexpr static Kind GetKind() {
        if constexpr (std::is_same_v<ValueType, uint64_t>)
            return Kind::kUint64;
        if constexpr (std::is_same_v<ValueType, int64_t>)
            return Kind::kInt64;
        if constexpr (std::is_same_v<ValueType, uint32_t>)
            return Kind::kUint32;
        if constexpr (std::is_same_v<ValueType, int32_t>)
            return Kind::kInt32;
        if constexpr (std::is_same_v<ValueType, uint16_t>)
            return Kind::kUint16;
        if constexpr (std::is_same_v<ValueType, int16_t>)
            return Kind::kInt16;
        if constexpr (std::is_same_v<ValueType, uint8_t>)
            return Kind::kUint8;
        if constexpr (std::is_same_v<ValueType, int8_t>)
            return Kind::kInt8;
        if constexpr (std::is_same_v<ValueType, double>)
            return Kind::kFloat64;
        if constexpr (std::is_same_v<ValueType, float>)
            return Kind::kFloat32;
    }
};

using Size = NumericConstantValue<uint32_t>;

struct BoolConstantValue : ConstantValue {
    BoolConstantValue(bool value)
        : ConstantValue(ConstantValue::Kind::kBool), value(value) {}

    operator bool() const { return value; }

    friend bool operator==(const BoolConstantValue& l, const BoolConstantValue& r) {
        return l.value == r.value;
    }

    friend bool operator!=(const BoolConstantValue& l, const BoolConstantValue& r) {
        return l.value != r.value;
    }

    friend std::ostream& operator<<(std::ostream& os, const BoolConstantValue& v) {
        os << v.value;
        return os;
    }

    virtual bool Convert(Kind kind, std::unique_ptr<ConstantValue>* out_value) const override {
        assert(out_value != nullptr);
        switch (kind) {
        case Kind::kBool:
            *out_value = std::make_unique<BoolConstantValue>(value);
            return true;
        default:
            return false;
        }
    }

    bool value;
};

struct StringConstantValue : ConstantValue {
    explicit StringConstantValue(StringView value)
        : ConstantValue(ConstantValue::Kind::kString), value(value) {}

    friend std::ostream& operator<<(std::ostream& os, const StringConstantValue& v) {
        os << v.value.data();
        return os;
    }

    virtual bool Convert(Kind kind, std::unique_ptr<ConstantValue>* out_value) const override {
        assert(out_value != nullptr);
        switch (kind) {
        case Kind::kString:
            *out_value = std::make_unique<StringConstantValue>(StringView(value));
            return true;
        default:
            return false;
        }
    }

    StringView value;
};

struct Constant {
    virtual ~Constant() {}

    enum struct Kind {
        kIdentifier,
        kLiteral,
        kSynthesized,
    };

    explicit Constant(Kind kind)
        : kind(kind), value_(nullptr) {}

    bool IsResolved() const { return value_ != nullptr; }

    void ResolveTo(std::unique_ptr<ConstantValue> value) {
        assert(value != nullptr);
        assert(!IsResolved() && "Constants should only be resolved once!");
        value_ = std::move(value);
    }

    const ConstantValue& Value() const {
        assert(IsResolved() && "Accessing the value of an unresolved Constant!");
        return *value_;
    }

    const Kind kind;

protected:
    std::unique_ptr<ConstantValue> value_;
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

struct SynthesizedConstant : Constant {
    explicit SynthesizedConstant(std::unique_ptr<ConstantValue> value)
        : Constant(Kind::kSynthesized) {
        ResolveTo(std::move(value));
    }
};

struct Decl {
    virtual ~Decl() {}

    enum struct Kind {
        kBits,
        kConst,
        kEnum,
        kInterface,
        kStruct,
        kTable,
        kUnion,
        kXUnion,
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

struct TypeDecl : public Decl {
    TypeDecl(Kind kind, std::unique_ptr<raw::AttributeList> attributes, Name name)
        : Decl(kind, std::move(attributes), std::move(name)) {}
    TypeShape typeshape;
    bool recursive = false;
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

    explicit Type(Kind kind, types::Nullability nullability, TypeShape shape)
        : kind(kind), nullability(nullability), shape(shape) {}

    const Kind kind;
    const types::Nullability nullability;
    TypeShape shape;

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
    ArrayType(const Type* element_type, const Size* element_count)
        : Type(
              Kind::kArray,
              types::Nullability::kNonnullable,
              Shape(element_type->shape, element_count->value)),
          element_type(element_type), element_count(element_count) {}

    const Type* element_type;
    const Size* element_count;

    Comparison Compare(const Type& other) const override {
        const auto& o = static_cast<const ArrayType&>(other);
        return Type::Compare(o)
            .Compare(element_count->value, o.element_count->value)
            .Compare(*element_type, *o.element_type);
    }

    static TypeShape Shape(TypeShape element, uint32_t count);
};

struct VectorType : public Type {
    VectorType(const Type* element_type, const Size* element_count, types::Nullability nullability)
        : Type(Kind::kVector, nullability, Shape(element_type->shape, element_count->value)),
          element_type(element_type), element_count(element_count) {}

    const Type* element_type;
    const Size* element_count;

    Comparison Compare(const Type& other) const override {
        const auto& o = static_cast<const VectorType&>(other);
        return Type::Compare(o)
            .Compare(element_count->value, o.element_count->value)
            .Compare(*element_type, *o.element_type);
    }

    static TypeShape Shape(TypeShape element, uint32_t max_element_count);
};

struct StringType : public Type {
    StringType(const Size* max_size, types::Nullability nullability)
        : Type(Kind::kString, nullability, Shape(max_size->value)), max_size(max_size) {}

    const Size* max_size;

    Comparison Compare(const Type& other) const override {
        const auto& o = static_cast<const StringType&>(other);
        return Type::Compare(o)
            .Compare(max_size->value, o.max_size->value);
    }

    static TypeShape Shape(uint32_t max_length);
};

struct HandleType : public Type {
    HandleType(types::HandleSubtype subtype, types::Nullability nullability)
        : Type(Kind::kHandle, nullability, Shape()),
          subtype(subtype) {}

    const types::HandleSubtype subtype;

    Comparison Compare(const Type& other) const override {
        const auto& o = *static_cast<const HandleType*>(&other);
        return Type::Compare(o)
            .Compare(subtype, o.subtype);
    }

    static TypeShape Shape();
};

struct PrimitiveType : public Type {

    explicit PrimitiveType(types::PrimitiveSubtype subtype)
        : Type(
            Kind::kPrimitive,
            types::Nullability::kNonnullable,
            Shape(subtype)),
          subtype(subtype) {}

    types::PrimitiveSubtype subtype;

    Comparison Compare(const Type& other) const override {
        const auto& o = static_cast<const PrimitiveType&>(other);
        return Type::Compare(o)
            .Compare(subtype, o.subtype);
    }

    static TypeShape Shape(types::PrimitiveSubtype subtype);
    static uint32_t SubtypeSize(types::PrimitiveSubtype subtype);
};

struct IdentifierType : public Type {
    IdentifierType(Name name, types::Nullability nullability, const TypeDecl* type_decl, TypeShape shape)
        : Type(Kind::kIdentifier, nullability, shape),
          name(std::move(name)), type_decl(type_decl) {}

    Name name;
    const TypeDecl* type_decl;

    Comparison Compare(const Type& other) const override {
        const auto& o = static_cast<const IdentifierType&>(other);
        return Type::Compare(o)
            .Compare(name, o.name);
    }
};

struct RequestHandleType : public Type {
    RequestHandleType(const IdentifierType* interface_type, types::Nullability nullability)
        : Type(Kind::kRequestHandle, nullability, HandleType::Shape()),
          interface_type(interface_type) {}

    const IdentifierType* interface_type;

    Comparison Compare(const Type& other) const override {
        const auto& o = static_cast<const RequestHandleType&>(other);
        return Type::Compare(o)
            .Compare(*interface_type, *o.interface_type);
    }
};

struct TypeConstructor {
    TypeConstructor(Name name, std::unique_ptr<TypeConstructor> maybe_arg_type_ctor,
               std::unique_ptr<types::HandleSubtype> maybe_handle_subtype,
               std::unique_ptr<Constant> maybe_size, types::Nullability nullability)
        : name(std::move(name)), maybe_arg_type_ctor(std::move(maybe_arg_type_ctor)),
          maybe_handle_subtype(std::move(maybe_handle_subtype)),
          maybe_size(std::move(maybe_size)), nullability(nullability) {}

    // Set during construction.
    const Name name;
    const std::unique_ptr<TypeConstructor> maybe_arg_type_ctor;
    const std::unique_ptr<types::HandleSubtype> maybe_handle_subtype;
    const std::unique_ptr<Constant> maybe_size;
    const types::Nullability nullability;

    // Set during compilation.
    bool compiling = false;
    bool compiled = false;
    const Type* type = nullptr;
};

struct Using {
    Using(Name name, const PrimitiveType* type)
        : name(std::move(name)), type(type) {}

    const Name name;
    const PrimitiveType* type;
};

struct Const : public Decl {
    Const(std::unique_ptr<raw::AttributeList> attributes, Name name,
          std::unique_ptr<TypeConstructor> type_ctor,
          std::unique_ptr<Constant> value)
        : Decl(Kind::kConst, std::move(attributes), std::move(name)), type_ctor(std::move(type_ctor)),
          value(std::move(value)) {}
    std::unique_ptr<TypeConstructor> type_ctor;
    std::unique_ptr<Constant> value;
};

struct Enum : public TypeDecl {
    struct Member {
        Member(SourceLocation name, std::unique_ptr<Constant> value, std::unique_ptr<raw::AttributeList> attributes)
            : name(name), value(std::move(value)), attributes(std::move(attributes)) {}
        SourceLocation name;
        std::unique_ptr<Constant> value;
        std::unique_ptr<raw::AttributeList> attributes;
    };

    Enum(std::unique_ptr<raw::AttributeList> attributes, Name name,
         std::unique_ptr<TypeConstructor> subtype_ctor,
         std::vector<Member> members)
        : TypeDecl(Kind::kEnum, std::move(attributes), std::move(name)),
          subtype_ctor(std::move(subtype_ctor)),
          members(std::move(members)) {}

    // Set during construction.
    std::unique_ptr<TypeConstructor> subtype_ctor;
    std::vector<Member> members;

    // Set during compilation.
    const PrimitiveType* type = nullptr;
};

struct Bits : public TypeDecl {
    struct Member {
        Member(SourceLocation name, std::unique_ptr<Constant> value, std::unique_ptr<raw::AttributeList> attributes)
            : name(name), value(std::move(value)), attributes(std::move(attributes)) {}
        SourceLocation name;
        std::unique_ptr<Constant> value;
        std::unique_ptr<raw::AttributeList> attributes;
    };

    Bits(std::unique_ptr<raw::AttributeList> attributes, Name name,
         std::unique_ptr<TypeConstructor> subtype_ctor,
         std::vector<Member> members)
        : TypeDecl(Kind::kBits, std::move(attributes), std::move(name)),
          subtype_ctor(std::move(subtype_ctor)),
          members(std::move(members)) {}

    std::unique_ptr<TypeConstructor> subtype_ctor;
    std::vector<Member> members;
};

struct Struct : public TypeDecl {
    struct Member {
        Member(std::unique_ptr<TypeConstructor> type_ctor, SourceLocation name,
               std::unique_ptr<Constant> maybe_default_value,
               std::unique_ptr<raw::AttributeList> attributes)
            : type_ctor(std::move(type_ctor)), name(std::move(name)),
              maybe_default_value(std::move(maybe_default_value)),
              attributes(std::move(attributes)) {}
        std::unique_ptr<TypeConstructor> type_ctor;
        SourceLocation name;
        std::unique_ptr<Constant> maybe_default_value;
        std::unique_ptr<raw::AttributeList> attributes;
        FieldShape fieldshape;
    };

    Struct(std::unique_ptr<raw::AttributeList> attributes, Name name,
           std::vector<Member> members, bool anonymous = false)
        : TypeDecl(Kind::kStruct, std::move(attributes), std::move(name)),
          members(std::move(members)), anonymous(anonymous) {
    }

    std::vector<Member> members;
    const bool anonymous;

    static TypeShape Shape(std::vector<FieldShape*>* fields, uint32_t extra_handles = 0u);
};

struct Table : public TypeDecl {
    struct Member {
        Member(std::unique_ptr<raw::Ordinal> ordinal, std::unique_ptr<TypeConstructor> type,
               SourceLocation name,
               std::unique_ptr<Constant> maybe_default_value,
               std::unique_ptr<raw::AttributeList> attributes)
            : ordinal(std::move(ordinal)),
              maybe_used(std::make_unique<Used>(std::move(type), std::move(name),
                                                std::move(maybe_default_value),
                                                std::move(attributes))) {}
        Member(std::unique_ptr<raw::Ordinal> ordinal, SourceLocation location)
            : ordinal(std::move(ordinal)),
              maybe_location(std::make_unique<SourceLocation>(location)) {}
        std::unique_ptr<raw::Ordinal> ordinal;
        // The location for reserved table members.
        std::unique_ptr<SourceLocation> maybe_location;
        struct Used {
            Used(std::unique_ptr<TypeConstructor> type_ctor, SourceLocation name,
                 std::unique_ptr<Constant> maybe_default_value,
                 std::unique_ptr<raw::AttributeList> attributes)
                : type_ctor(std::move(type_ctor)), name(std::move(name)),
                  maybe_default_value(std::move(maybe_default_value)),
                  attributes(std::move(attributes)) {}
            std::unique_ptr<TypeConstructor> type_ctor;
            SourceLocation name;
            std::unique_ptr<Constant> maybe_default_value;
            std::unique_ptr<raw::AttributeList> attributes;
            TypeShape typeshape;
        };
        std::unique_ptr<Used> maybe_used;
    };

    Table(std::unique_ptr<raw::AttributeList> attributes, Name name, std::vector<Member> members)
        : TypeDecl(Kind::kTable, std::move(attributes), std::move(name)),
                   members(std::move(members)) {}

    std::vector<Member> members;

    static TypeShape Shape(std::vector<TypeShape*>* fields, uint32_t extra_handles = 0u);
};

struct Union : public TypeDecl {
    struct Member {
        Member(std::unique_ptr<TypeConstructor> type_ctor, SourceLocation name,
               std::unique_ptr<raw::AttributeList> attributes)
            : type_ctor(std::move(type_ctor)), name(std::move(name)),
              attributes(std::move(attributes)) {}
        std::unique_ptr<TypeConstructor> type_ctor;
        SourceLocation name;
        std::unique_ptr<raw::AttributeList> attributes;
        FieldShape fieldshape;
    };

    Union(std::unique_ptr<raw::AttributeList> attributes, Name name, std::vector<Member> members)
        : TypeDecl(Kind::kUnion, std::move(attributes), std::move(name)),
                   members(std::move(members)) {}

    std::vector<Member> members;
    // The offset of each of the union members is the same, so store
    // it here as well.
    FieldShape membershape;

    static TypeShape Shape(const std::vector<flat::Union::Member>& members);
};

struct XUnion : public TypeDecl {
    struct Member {
        Member(std::unique_ptr<raw::Ordinal> ordinal, std::unique_ptr<TypeConstructor> type_ctor,
               SourceLocation name, std::unique_ptr<raw::AttributeList> attributes)
            : ordinal(std::move(ordinal)), type_ctor(std::move(type_ctor)), name(std::move(name)),
              attributes(std::move(attributes)) {}
        std::unique_ptr<raw::Ordinal> ordinal;
        std::unique_ptr<TypeConstructor> type_ctor;
        SourceLocation name;
        std::unique_ptr<raw::AttributeList> attributes;
        FieldShape fieldshape;
    };

    XUnion(std::unique_ptr<raw::AttributeList> attributes, Name name, std::vector<Member> members)
        : TypeDecl(Kind::kXUnion, std::move(attributes), std::move(name)), members(std::move(members)) {}

    std::vector<Member> members;

    static TypeShape Shape(const std::vector<flat::XUnion::Member>& members, uint32_t extra_handles = 0u);
};

struct Interface : public TypeDecl {
    struct Method {
        Method(Method&&) = default;
        Method& operator=(Method&&) = default;

        Method(std::unique_ptr<raw::AttributeList> attributes,
               std::unique_ptr<raw::Ordinal> ordinal,
               std::unique_ptr<raw::Ordinal> generated_ordinal,
               SourceLocation name,
               Struct* maybe_request,
               Struct* maybe_response)
            : attributes(std::move(attributes)),
              ordinal(std::move(ordinal)),
              generated_ordinal(std::move(generated_ordinal)),
              name(std::move(name)),
              maybe_request(maybe_request),
              maybe_response(maybe_response) {
            assert(this->maybe_request != nullptr || this->maybe_response != nullptr);
        }

        std::unique_ptr<raw::AttributeList> attributes;
        std::unique_ptr<raw::Ordinal> ordinal;
        std::unique_ptr<raw::Ordinal> generated_ordinal;
        SourceLocation name;
        Struct* maybe_request;
        Struct* maybe_response;
    };

    Interface(std::unique_ptr<raw::AttributeList> attributes, Name name,
              std::set<Name> superinterfaces, std::vector<Method> methods)
        : TypeDecl(Kind::kInterface, std::move(attributes), std::move(name)),
          superinterfaces(std::move(superinterfaces)), methods(std::move(methods)) {}

    std::set<Name> superinterfaces;
    std::vector<Method> methods;
    // Pointers here are set after superinterfaces are compiled, and
    // are owned by the correspending superinterface.
    std::vector<const Method*> all_methods;
};

class TypeTemplate {
public:
    TypeTemplate(Name name, Typespace* typespace, ErrorReporter* error_reporter)
        : typespace_(typespace), name_(std::move(name)), error_reporter_(error_reporter) {}

    TypeTemplate(TypeTemplate&& type_template) = default;

    virtual ~TypeTemplate() = default;

    const Name* name() const { return &name_; }

    virtual bool Create(const SourceLocation* maybe_location,
                        const Type* arg_type,
                        const types::HandleSubtype* handle_subtype,
                        const Size* size,
                        types::Nullability nullability,
                        std::unique_ptr<Type>* out_type) const = 0;

protected:
    bool MustBeParameterized(const SourceLocation* maybe_location) const {
        return Fail(maybe_location, "must be parametrized");
    }
    bool MustHaveSize(const SourceLocation* maybe_location) const {
        return Fail(maybe_location, "must have size");
    }
    bool CannotBeParameterized(const SourceLocation* maybe_location) const {
        return Fail(maybe_location, "cannot be parametrized");
    }
    bool CannotHaveSize(const SourceLocation* maybe_location) const {
        return Fail(maybe_location, "cannot have size");
    }
    bool CannotBeNullable(const SourceLocation* maybe_location) const {
        return Fail(maybe_location, "cannot be nullable");
    }
    bool Fail(const SourceLocation* maybe_location, const std::string& content) const;

    Typespace* typespace_;

private:
    Name name_;
    ErrorReporter* error_reporter_;
};

// Typespace provides builders for all types (e.g. array, vector, string), and
// ensures canonicalization, i.e. the same type is represented by one object,
// shared amongst all uses of said type. For instance, while the text
// `vector<uint8>:7` may appear multiple times in source, these all indicate
// the same type.
class Typespace {
public:
    explicit Typespace(ErrorReporter* error_reporter)
        : error_reporter_(error_reporter) {}

    bool Create(const flat::Name& name,
                const Type* arg_type,
                const types::HandleSubtype* handle_subtype,
                const Size* size,
                types::Nullability nullability,
                const Type** out_type);

    void AddTemplate(std::unique_ptr<TypeTemplate> type_template);

    // BoostrapRootTypes creates a instance with all primitive types. It is
    // meant to be used as the top-level types lookup mechanism, providing
    // definitional meaning to names such as `int64`, or `bool`.
    static Typespace RootTypes(ErrorReporter* error_reporter);

private:
    friend class TypeAliasTypeTemplate;

    bool CreateNotOwned(const flat::Name& name,
                        const Type* arg_type,
                        const types::HandleSubtype* handle_subtype,
                        const Size* size,
                        types::Nullability nullability,
                        std::unique_ptr<Type>* out_type);
    const TypeTemplate* LookupTemplate(const flat::Name& name) const;

    struct cmpName {
        bool operator()(const flat::Name* a, const flat::Name* b) const {
            return *a < *b;
        }
    };

    std::map<const flat::Name*, std::unique_ptr<TypeTemplate>, cmpName> templates_;
    std::vector<std::unique_ptr<Type>> types_;

    ErrorReporter* error_reporter_;
};

// AttributeSchema defines a schema for attributes. This includes:
// - The allowed placement of an attribute (e.g. on a method, on a struct
//   declaration);
// - The allowed values which an attribute can take.
// For attributes which may be placed on declarations (e.g. interface, struct,
// union, table), a schema may additionally include:
// - A constraint which must be met by the declaration.
class AttributeSchema {
public:
    using Constraint = fit::function<bool(ErrorReporter* error_reporter,
                                          const raw::Attribute& attribute,
                                          const Decl* decl)>;

    // Placement indicates the placement of an attribute, e.g. whether an
    // attribute is placed on an enum declaration, method, or union
    // member.
    enum class Placement {
        kBitsDecl,
        kBitsMember,
        kConstDecl,
        kEnumDecl,
        kEnumMember,
        kInterfaceDecl,
        kLibrary,
        kMethod,
        kStructDecl,
        kStructMember,
        kTableDecl,
        kTableMember,
        kUnionDecl,
        kUnionMember,
        kXUnionDecl,
        kXUnionMember,
    };

    AttributeSchema(const std::set<Placement>& allowed_placements,
                    const std::set<std::string> allowed_values,
                    Constraint constraint = NoOpConstraint);

    AttributeSchema(AttributeSchema&& schema) = default;

    void ValidatePlacement(ErrorReporter* error_reporter,
                           const raw::Attribute& attribute,
                           Placement placement) const;

    void ValidateValue(ErrorReporter* error_reporter,
                       const raw::Attribute& attribute) const;

    void ValidateConstraint(ErrorReporter* error_reporter,
                            const raw::Attribute& attribute,
                            const Decl* decl) const;

private:
    static bool NoOpConstraint(ErrorReporter* error_reporter,
                               const raw::Attribute& attribute,
                               const Decl* decl) {
        return true;
    }

    std::set<Placement> allowed_placements_;
    std::set<std::string> allowed_values_;
    Constraint constraint_;
};

class Libraries {
public:
    Libraries();

    // Insert |library|.
    bool Insert(std::unique_ptr<Library> library);

    // Lookup a library by its |library_name|.
    bool Lookup(const std::vector<StringView>& library_name,
                Library** out_library) const;

    void AddAttributeSchema(const std::string& name, AttributeSchema schema) {
        [[maybe_unused]] auto iter =
            attribute_schemas_.emplace(name, std::move(schema));
        assert(iter.second && "do not add schemas twice");
    }

    const AttributeSchema* RetrieveAttributeSchema(ErrorReporter* error_reporter,
                                                   const raw::Attribute& attribute) const;

private:
    std::map<std::vector<StringView>, std::unique_ptr<Library>> all_libraries_;
    std::map<std::string, AttributeSchema> attribute_schemas_;
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

    const std::set<Library*>& dependencies() const { return dependencies_aggregate_; }

private:
    bool InsertByName(StringView filename, const std::vector<StringView>& name,
                      Library* library);

    using ByName = std::map<std::vector<StringView>, Library*>;
    using ByFilename = std::map<std::string, std::unique_ptr<ByName>>;

    ByFilename dependencies_;
    std::set<Library*> dependencies_aggregate_;
};

class Library {
public:
    Library(const Libraries* all_libraries, ErrorReporter* error_reporter, Typespace* typespace)
        : all_libraries_(all_libraries), error_reporter_(error_reporter), typespace_(typespace) {}

    bool ConsumeFile(std::unique_ptr<raw::File> file);
    bool Compile();

    const std::vector<StringView>& name() const { return library_name_; }
    const std::vector<std::string>& errors() const { return error_reporter_->errors(); }

private:
    friend class TypeAliasTypeTemplate;

    bool Fail(StringView message);
    bool Fail(const SourceLocation& location, StringView message) {
        return Fail(&location, message);
    }
    bool Fail(const SourceLocation* maybe_location, StringView message);
    bool Fail(const Name& name, StringView message) {
        return Fail(name.maybe_location(), message);
    }
    bool Fail(const Decl& decl, StringView message) { return Fail(decl.name, message); }

    void ValidateAttributesPlacement(AttributeSchema::Placement placement,
                                     const raw::AttributeList* attributes);
    void ValidateAttributesConstraints(const Decl* decl,
                                       const raw::AttributeList* attributes);

    // TODO(FIDL-596): Rationalize the use of names. Here, a simple name is
    // one that is not scoped, it is just text. An anonymous name is one that
    // is guaranteed to be unique within the library, and a derived name is one
    // that is library scoped but derived from the concatenated components using
    // underscores as delimiters.
    SourceLocation GeneratedSimpleName(const std::string& name);
    Name NextAnonymousName();
    Name DerivedName(const std::vector<StringView>& components);

    bool CompileCompoundIdentifier(const raw::CompoundIdentifier* compound_identifier,
                                   SourceLocation location, Name* out_name);
    void RegisterConst(Const* decl);
    bool RegisterDecl(Decl* decl);

    bool ConsumeConstant(std::unique_ptr<raw::Constant> raw_constant, SourceLocation location,
                         std::unique_ptr<Constant>* out_constant);
    bool ConsumeTypeConstructor(std::unique_ptr<raw::TypeConstructor> raw_type_ctor,
                                SourceLocation location,
                                std::unique_ptr<TypeConstructor>* out_type);

    bool ConsumeUsing(std::unique_ptr<raw::Using> using_directive);
    bool ConsumeTypeAlias(std::unique_ptr<raw::Using> using_directive);
    bool ConsumeBitsDeclaration(std::unique_ptr<raw::BitsDeclaration> bits_declaration);
    bool ConsumeConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const_declaration);
    bool ConsumeEnumDeclaration(std::unique_ptr<raw::EnumDeclaration> enum_declaration);
    bool
    ConsumeInterfaceDeclaration(std::unique_ptr<raw::InterfaceDeclaration> interface_declaration);
    bool ConsumeParameterList(Name name, std::unique_ptr<raw::ParameterList> parameter_list,
                              bool anonymous, Struct** out_struct_decl);
    bool CreateMethodResult(const Name& interface_name, raw::InterfaceMethod* method, Struct* in_response, Struct** out_response);
    bool ConsumeStructDeclaration(std::unique_ptr<raw::StructDeclaration> struct_declaration);
    bool ConsumeTableDeclaration(std::unique_ptr<raw::TableDeclaration> table_declaration);
    bool ConsumeUnionDeclaration(std::unique_ptr<raw::UnionDeclaration> union_declaration);
    bool ConsumeXUnionDeclaration(std::unique_ptr<raw::XUnionDeclaration> xunion_declaration);

    bool TypeCanBeConst(const Type* type);
    const Type* TypeResolve(const Type* type);
    bool TypeIsConvertibleTo(const Type* from_type, const Type* to_type);
    std::unique_ptr<TypeConstructor> IdentifierTypeForDecl(const Decl* decl, types::Nullability nullability);

    // Given a const declaration of the form
    //     const type foo = name;
    // return the declaration corresponding to name.
    Decl* LookupConstant(const TypeConstructor* type_ctor, const Name& name);

    bool DeclDependencies(Decl* decl, std::set<Decl*>* out_edges);

    bool SortDeclarations();

    bool CompileLibraryName();

    bool CompileBits(Bits* bits_declaration);
    bool CompileConst(Const* const_declaration);
    bool CompileEnum(Enum* enum_declaration);
    bool CompileInterface(Interface* interface_declaration);
    bool CompileStruct(Struct* struct_declaration);
    bool CompileTable(Table* table_declaration);
    bool CompileUnion(Union* union_declaration);
    bool CompileXUnion(XUnion* xunion_declaration);

    // Compiling a type both validates the type, and computes shape
    // information for the type. In particular, we validate that
    // optional identifier types refer to things that can in fact be
    // nullable (ie not enums).
    bool CompileTypeConstructor(TypeConstructor* type, TypeShape* out_type_metadata);

    bool ResolveConstant(Constant* constant, const Type* type);
    bool ResolveIdentifierConstant(IdentifierConstant* identifier_constant, const Type* type);
    bool ResolveLiteralConstant(LiteralConstant* literal_constant, const Type* type);

    // Validates a single member of a bits or enum. On failure,
    // returns false and places an error message in the out parameter.
    template <typename MemberType>
    using MemberValidator = fit::function<bool(const MemberType& member, std::string* out_error)>;
    template <typename DeclType, typename MemberType>
    bool ValidateMembers(DeclType* decl, MemberValidator<MemberType> validator);
    template <typename MemberType>
    bool ValidateBitsMembers(Bits* bits_decl);
    template <typename MemberType>
    bool ValidateEnumMembers(Enum* enum_decl);

    bool VerifyDeclAttributes(Decl* decl);

public:
    bool CompileDecl(Decl* decl);

    // Returns nullptr when the |name| cannot be resolved to a
    // Name. Otherwise it returns the declaration.
    Decl* LookupDeclByName(const Name& name) const;

    template <typename NumericType>
    bool ParseNumericLiteral(const raw::NumericLiteral* literal, NumericType* out_value) const;

    bool HasAttribute(fidl::StringView name) const;

    const std::set<Library*>& dependencies() const;

    std::vector<StringView> library_name_;

    std::vector<std::unique_ptr<Bits>> bits_declarations_;
    std::vector<std::unique_ptr<Const>> const_declarations_;
    std::vector<std::unique_ptr<Enum>> enum_declarations_;
    std::vector<std::unique_ptr<Interface>> interface_declarations_;
    std::vector<std::unique_ptr<Struct>> struct_declarations_;
    std::vector<std::unique_ptr<Table>> table_declarations_;
    std::vector<std::unique_ptr<Union>> union_declarations_;
    std::vector<std::unique_ptr<XUnion>> xunion_declarations_;

    // All Decl pointers here are non-null and are owned by the
    // various foo_declarations_.
    std::vector<Decl*> declaration_order_;

private:
    const PrimitiveType kSizeType = PrimitiveType(types::PrimitiveSubtype::kUint32);

    std::unique_ptr<raw::AttributeList> attributes_;

    Dependencies dependencies_;
    const Libraries* all_libraries_;

    // All Name, Constant, Using, and Decl pointers here are non-null and are
    // owned by the various foo_declarations_.
    std::map<const Name*, Decl*, PtrCompare<Name>> declarations_;
    std::map<const Name*, Const*, PtrCompare<Name>> constants_;

    ErrorReporter* error_reporter_;
    Typespace* typespace_;

    uint32_t anon_counter_ = 0;

    VirtualSourceFile generated_source_file_{"generated"};
};

} // namespace flat
} // namespace fidl

// TODO(FIDL-487, ZX-3415): Decide if all cases of NumericConstantValue::Convert() are safe.
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FLAT_AST_H_
