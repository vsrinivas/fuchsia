// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat_ast.h"

#include <assert.h>
#include <stdio.h>

#include <algorithm>
#include <regex>
#include <sstream>

#include "fidl/attributes.h"
#include "fidl/lexer.h"
#include "fidl/names.h"
#include "fidl/ordinals.h"
#include "fidl/parser.h"
#include "fidl/raw_ast.h"
#include "fidl/utils.h"

namespace fidl {
namespace flat {

namespace {

class ScopeInsertResult {
public:
    explicit ScopeInsertResult(std::unique_ptr<SourceLocation> previous_occurrence)
        : previous_occurrence_(std::move(previous_occurrence)) {}

    static ScopeInsertResult Ok() { return ScopeInsertResult(nullptr); }
    static ScopeInsertResult FailureAt(SourceLocation previous) {
        return ScopeInsertResult(std::make_unique<SourceLocation>(previous));
    }

    bool ok() const {
        return previous_occurrence_ == nullptr;
    }

    const SourceLocation& previous_occurrence() const {
        assert(!ok());
        return *previous_occurrence_;
    }

private:
    std::unique_ptr<SourceLocation> previous_occurrence_;
};

template <typename T>
class Scope {
public:
    ScopeInsertResult Insert(const T& t, SourceLocation location) {
        auto iter = scope_.find(t);
        if (iter != scope_.end()) {
            return ScopeInsertResult::FailureAt(iter->second);
        } else {
            scope_.emplace(t, location);
            return ScopeInsertResult::Ok();
        }
    }

    typename std::map<T, SourceLocation>::const_iterator begin() const {
        return scope_.begin();
    }

    typename std::map<T, SourceLocation>::const_iterator end() const {
        return scope_.end();
    }

private:
    std::map<T, SourceLocation> scope_;
};

struct MethodScope {
    Scope<uint32_t> ordinals;
    Scope<StringView> names;
    Scope<const Interface*> interfaces;
};

// A helper class to track when a Decl is compiling and compiled.
class Compiling {
public:
    explicit Compiling(Decl* decl)
        : decl_(decl) {
        decl_->compiling = true;
    }
    ~Compiling() {
        decl_->compiling = false;
        decl_->compiled = true;
    }

private:
    Decl* decl_;
};

} // namespace

constexpr uint32_t kMessageAlign = 8u;

uint32_t AlignTo(uint64_t size, uint64_t alignment) {
    return static_cast<uint32_t>(
        std::min((size + alignment - 1) & -alignment,
                 static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
}

uint32_t ClampedMultiply(uint32_t a, uint32_t b) {
    return static_cast<uint32_t>(
        std::min(static_cast<uint64_t>(a) * static_cast<uint64_t>(b),
                 static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
}

uint32_t ClampedAdd(uint32_t a, uint32_t b) {
    return static_cast<uint32_t>(
        std::min(static_cast<uint64_t>(a) + static_cast<uint64_t>(b),
                 static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
}

TypeShape AlignTypeshape(TypeShape shape, uint32_t alignment) {
    uint32_t new_alignment = std::max(shape.Alignment(), alignment);
    uint32_t new_size = AlignTo(shape.Size(), new_alignment);
    return TypeShape(new_size, new_alignment, shape.Depth(), shape.MaxHandles(), shape.MaxOutOfLine());
}

TypeShape Struct::Shape(std::vector<FieldShape*>* fields, uint32_t extra_handles) {
    uint32_t size = 0u;
    uint32_t alignment = 1u;
    uint32_t depth = 0u;
    uint32_t max_handles = 0u;
    uint32_t max_out_of_line = 0u;

    for (FieldShape* field : *fields) {
        TypeShape typeshape = field->Typeshape();
        alignment = std::max(alignment, typeshape.Alignment());
        size = AlignTo(size, typeshape.Alignment());
        field->SetOffset(size);
        size += typeshape.Size();
        depth = std::max(depth, typeshape.Depth());
        max_handles = ClampedAdd(max_handles, typeshape.MaxHandles());
        max_out_of_line = ClampedAdd(max_out_of_line, typeshape.MaxOutOfLine());
    }

    max_handles = ClampedAdd(max_handles, extra_handles);

    size = AlignTo(size, alignment);

    if (fields->empty()) {
        assert(size == 0);
        assert(alignment == 1);

        // Empty structs are defined to have a size of 1 (a single byte).
        size = 1;
    }

    return TypeShape(size, alignment, depth, max_handles, max_out_of_line);
}

TypeShape Union::Shape(const std::vector<flat::Union::Member>& members) {
    uint32_t size = 0u;
    uint32_t alignment = 1u;
    uint32_t depth = 0u;
    uint32_t max_handles = 0u;
    uint32_t max_out_of_line = 0u;

    for (const auto& member : members) {
        const auto& fieldshape = member.fieldshape;
        size = std::max(size, fieldshape.Size());
        alignment = std::max(alignment, fieldshape.Alignment());
        depth = std::max(depth, fieldshape.Depth());
        max_handles = std::max(max_handles, fieldshape.Typeshape().MaxHandles());
        max_out_of_line = std::max(max_out_of_line, fieldshape.Typeshape().MaxOutOfLine());
    }

    size = AlignTo(size, alignment);
    return TypeShape(size, alignment, depth, max_handles, max_out_of_line);
}

TypeShape FidlMessageTypeShape(std::vector<FieldShape*>* fields) {
    auto struct_shape = Struct::Shape(fields);
    return AlignTypeshape(struct_shape, kMessageAlign);
}

TypeShape PointerTypeShape(const TypeShape& element, uint32_t max_element_count = 1u) {
    // Because FIDL supports recursive data structures, we might not have
    // computed the TypeShape for the element we're pointing to. In that case,
    // the size will be zero and we'll use |numeric_limits<uint32_t>::max()| as
    // the depth. We'll never see a zero size for a real TypeShape because empty
    // structs are banned.
    //
    // We're careful to check for saturation before incrementing the depth
    // because recursive data structures have a depth pegged at the numeric
    // limit.
    uint32_t depth = std::numeric_limits<uint32_t>::max();
    if (element.Size() > 0 && element.Depth() < std::numeric_limits<uint32_t>::max())
        depth = ClampedAdd(element.Depth(), 1);

    // The element(s) will be stored out-of-line.
    uint32_t elements_size = ClampedMultiply(element.Size(), max_element_count);
    // Out-of-line data is aligned to 8 bytes.
    elements_size = AlignTo(elements_size, 8);
    // The elements may each carry their own out-of-line data.
    uint32_t elements_out_of_line = ClampedMultiply(element.MaxOutOfLine(), max_element_count);

    uint32_t max_handles = ClampedMultiply(element.MaxHandles(), max_element_count);
    uint32_t max_out_of_line = ClampedAdd(elements_size, elements_out_of_line);

    return TypeShape(8u, 8u, depth, max_handles, max_out_of_line);
}

TypeShape CEnvelopeTypeShape(const TypeShape& contained_type) {
    auto packed_sizes_field = FieldShape(PrimitiveType::Shape(types::PrimitiveSubtype::kUint64));
    auto pointer_type = FieldShape(PointerTypeShape(contained_type));
    std::vector<FieldShape*> header{&packed_sizes_field, &pointer_type};
    return Struct::Shape(&header);
}

TypeShape Table::Shape(std::vector<TypeShape*>* fields, uint32_t extra_handles) {
    uint32_t element_depth = 0u;
    uint32_t max_handles = 0u;
    uint32_t max_out_of_line = 0u;
    uint32_t array_size = 0u;
    for (auto field : *fields) {
        if (field == nullptr) {
            continue;
        }
        auto envelope = CEnvelopeTypeShape(*field);
        element_depth = std::max(element_depth, envelope.Depth());
        array_size = ClampedAdd(array_size, envelope.Size());
        max_handles = ClampedAdd(max_handles, envelope.MaxHandles());
        max_out_of_line = ClampedAdd(max_out_of_line, envelope.MaxOutOfLine());
        assert(envelope.Alignment() == 8u);
    }
    auto pointer_element = TypeShape(array_size, 8u, 1 + element_depth,
                                     max_handles, max_out_of_line);
    auto num_fields = FieldShape(PrimitiveType::Shape(types::PrimitiveSubtype::kUint32));
    auto data_field = FieldShape(PointerTypeShape(pointer_element));
    std::vector<FieldShape*> header{&num_fields, &data_field};
    return Struct::Shape(&header, extra_handles);
}

TypeShape XUnion::Shape(const std::vector<flat::XUnion::Member>& members, uint32_t extra_handles) {
    uint32_t depth = 0u;
    uint32_t max_handles = 0u;
    uint32_t max_out_of_line = 0u;

    for (const auto& member : members) {
        const auto& envelope = CEnvelopeTypeShape(member.fieldshape.Typeshape());

        depth = ClampedAdd(depth, envelope.Depth());
        max_handles = ClampedAdd(max_handles, envelope.MaxHandles());
        max_out_of_line = std::max(max_out_of_line, envelope.MaxOutOfLine());
    }

    return TypeShape(24u, 8u, depth, max_handles, max_out_of_line);
}

TypeShape ArrayType::Shape(TypeShape element, uint32_t count) {
    // TODO(FIDL-345): once TypeShape builders are done and methods can fail, do a
    // __builtin_mul_overflow and fail on overflow instead of ClampedMultiply(element.Size(), count)
    return TypeShape(ClampedMultiply(element.Size(), count),
                     element.Alignment(),
                     element.Depth(),
                     ClampedMultiply(element.MaxHandles(), count),
                     ClampedMultiply(element.MaxOutOfLine(), count));
}

TypeShape VectorType::Shape(TypeShape element, uint32_t max_element_count) {
    auto size = FieldShape(PrimitiveType::Shape(types::PrimitiveSubtype::kUint64));
    auto data = FieldShape(PointerTypeShape(element, max_element_count));
    std::vector<FieldShape*> header{&size, &data};
    return Struct::Shape(&header);
}

TypeShape StringType::Shape(uint32_t max_length) {
    auto size = FieldShape(PrimitiveType::Shape(types::PrimitiveSubtype::kInt64));
    auto data = FieldShape(PointerTypeShape(PrimitiveType::Shape(types::PrimitiveSubtype::kUint8), max_length));
    std::vector<FieldShape*> header{&size, &data};
    return Struct::Shape(&header, 0);
}

TypeShape HandleType::Shape() {
    return TypeShape(4u, 4u, 0u, 1u);
}

uint32_t PrimitiveType::SubtypeSize(types::PrimitiveSubtype subtype) {
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

TypeShape PrimitiveType::Shape(types::PrimitiveSubtype subtype) {
    return TypeShape(SubtypeSize(subtype), SubtypeSize(subtype));
}

bool Decl::HasAttribute(fidl::StringView name) const {
    if (!attributes)
        return false;
    return attributes->HasAttribute(name);
}

fidl::StringView Decl::GetAttribute(fidl::StringView name) const {
    if (!attributes)
        return fidl::StringView();
    for (const auto& attribute : attributes->attributes) {
        if (StringView(attribute.name) == name) {
            if (attribute.value != "") {
                const auto& value = attribute.value;
                return fidl::StringView(value.data(), value.size());
            }
            // Don't search for another attribute with the same name.
            break;
        }
    }
    return fidl::StringView();
}

std::string Decl::GetName() const {
    return name.name_part();
}

bool IsSimple(const Type* type, const FieldShape& fieldshape) {
    switch (type->kind) {
    case Type::Kind::kVector: {
        auto vector_type = static_cast<const VectorType*>(type);
        if (*vector_type->element_count == Size::Max())
            return false;
        switch (vector_type->element_type->kind) {
        case Type::Kind::kHandle:
        case Type::Kind::kRequestHandle:
        case Type::Kind::kPrimitive:
            return true;
        case Type::Kind::kArray:
        case Type::Kind::kVector:
        case Type::Kind::kString:
        case Type::Kind::kIdentifier:
            return false;
        }
    }
    case Type::Kind::kString: {
        auto string_type = static_cast<const StringType*>(type);
        return *string_type->max_size < Size::Max();
    }
    case Type::Kind::kArray:
    case Type::Kind::kHandle:
    case Type::Kind::kRequestHandle:
    case Type::Kind::kPrimitive:
        return fieldshape.Depth() == 0u;
    case Type::Kind::kIdentifier: {
        auto identifier_type = static_cast<const IdentifierType*>(type);
        switch (identifier_type->nullability) {
        case types::Nullability::kNullable:
            // If the identifier is nullable, then we can handle a depth of 1
            // because the secondary object is directly accessible.
            return fieldshape.Depth() <= 1u;
        case types::Nullability::kNonnullable:
            return fieldshape.Depth() == 0u;
        }
    }
    }
}

bool Typespace::Create(const flat::Name& name,
                       const Type* arg_type,
                       const std::optional<types::HandleSubtype>& handle_subtype,
                       const Size* size,
                       types::Nullability nullability,
                       const Type** out_type) {
    std::unique_ptr<Type> type;
    if (!CreateNotOwned(name, arg_type, handle_subtype, size, nullability, &type))
        return false;
    types_.push_back(std::move(type));
    *out_type = types_.back().get();
    return true;
}

bool Typespace::CreateNotOwned(const flat::Name& name,
                       const Type* arg_type,
                       const std::optional<types::HandleSubtype>& handle_subtype,
                       const Size* size,
                       types::Nullability nullability,
                       std::unique_ptr<Type>* out_type) {
    // TODO(pascallouis): lookup whether we've already created the type, and
    // return it rather than create a new one. Lookup must be by name,
    // arg_type, size, and nullability.

    auto maybe_location = name.maybe_location();
    auto type_template = LookupTemplate(name);
    if (type_template == nullptr) {
        std::string message("unknown type ");
        message.append(name.name_part());
        error_reporter_->ReportError(maybe_location, message);
        return false;
    }
    return type_template->Create(maybe_location, arg_type, handle_subtype, size,
                                 nullability, out_type);

}

void Typespace::AddTemplate(std::unique_ptr<TypeTemplate> type_template) {
    templates_.emplace(type_template->name(), std::move(type_template));
}

const TypeTemplate* Typespace::LookupTemplate(const flat::Name& name) const {
    Name global_name(nullptr, name.name_part());
    auto iter1 = templates_.find(&global_name);
    if (iter1 != templates_.end())
        return iter1->second.get();

    auto iter2 = templates_.find(&name);
    if (iter2 != templates_.end())
        return iter2->second.get();

    return nullptr;
}

bool TypeTemplate::Fail(const SourceLocation* maybe_location, const std::string& content) const {
    std::string message(NameName(name_, ".", "/"));
    message.append(" ");
    message.append(content);
    error_reporter_->ReportError(maybe_location, message);
    return false;
}

class PrimitiveTypeTemplate : public TypeTemplate {
public:
    PrimitiveTypeTemplate(Typespace* typespace, ErrorReporter* error_reporter,
                          const std::string& name, types::PrimitiveSubtype subtype)
        : TypeTemplate(Name(nullptr, name), typespace, error_reporter),
          subtype_(subtype) {}

    bool Create(const SourceLocation* maybe_location,
                const Type* maybe_arg_type,
                const std::optional<types::HandleSubtype>& no_handle_subtype,
                const Size* maybe_size,
                types::Nullability nullability,
                std::unique_ptr<Type>* out_type) const {
        assert(!no_handle_subtype.has_value());

        if (maybe_arg_type != nullptr)
            return CannotBeParameterized(maybe_location);
        if (maybe_size != nullptr)
            return CannotHaveSize(maybe_location);
        if (nullability == types::Nullability::kNullable)
            return CannotBeNullable(maybe_location);

        *out_type = std::make_unique<PrimitiveType>(subtype_);
        return true;
    }

private:
    const types::PrimitiveSubtype subtype_;
};

class BytesTypeTemplate : public TypeTemplate {
public:
    BytesTypeTemplate(Typespace* typespace, ErrorReporter* error_reporter)
        : TypeTemplate(Name(nullptr, "bytes"), typespace, error_reporter),
          uint8_type_(types::PrimitiveSubtype::kUint8) {}

    bool Create(const SourceLocation* maybe_location,
                const Type* maybe_arg_type,
                const std::optional<types::HandleSubtype>& no_handle_subtype,
                const Size* size,
                types::Nullability nullability,
                std::unique_ptr<Type>* out_type) const {
        assert(!no_handle_subtype.has_value());

        if (maybe_arg_type != nullptr)
            return CannotBeParameterized(maybe_location);
        if (size == nullptr)
            size = &max_size;

        *out_type = std::make_unique<VectorType>(&uint8_type_, size, nullability);
        return true;
    }

private:
    const PrimitiveType uint8_type_;
    Size max_size = Size::Max();
};

class ArrayTypeTemplate : public TypeTemplate {
public:
    ArrayTypeTemplate(Typespace* typespace, ErrorReporter* error_reporter)
        : TypeTemplate(Name(nullptr, "array"), typespace, error_reporter) {}

    bool Create(const SourceLocation* maybe_location,
                const Type* arg_type,
                const std::optional<types::HandleSubtype>& no_handle_subtype,
                const Size* size,
                types::Nullability nullability,
                std::unique_ptr<Type>* out_type) const {
        assert(!no_handle_subtype.has_value());

        if (arg_type == nullptr)
            return MustBeParameterized(maybe_location);
        if (size == nullptr)
            return MustHaveSize(maybe_location);
        if (nullability == types::Nullability::kNullable)
            return CannotBeNullable(maybe_location);

        *out_type = std::make_unique<ArrayType>(arg_type, size);
        return true;
    }
};

class VectorTypeTemplate : public TypeTemplate {
public:
    VectorTypeTemplate(Typespace* typespace, ErrorReporter* error_reporter)
        : TypeTemplate(Name(nullptr, "vector"), typespace, error_reporter) {}

    bool Create(const SourceLocation* maybe_location,
                const Type* arg_type,
                const std::optional<types::HandleSubtype>& no_handle_subtype,
                const Size* size,
                types::Nullability nullability,
                std::unique_ptr<Type>* out_type) const {
        assert(!no_handle_subtype.has_value());

        if (arg_type == nullptr)
            return MustBeParameterized(maybe_location);
        if (size == nullptr)
            size = &max_size;

        *out_type = std::make_unique<VectorType>(arg_type, size, nullability);
        return true;
    }

private:
    Size max_size = Size::Max();
};

class StringTypeTemplate : public TypeTemplate {
public:
    StringTypeTemplate(Typespace* typespace, ErrorReporter* error_reporter)
        : TypeTemplate(Name(nullptr, "string"), typespace, error_reporter) {}

    bool Create(const SourceLocation* maybe_location,
                const Type* arg_type,
                const std::optional<types::HandleSubtype>& no_handle_subtype,
                const Size* size,
                types::Nullability nullability,
                std::unique_ptr<Type>* out_type) const {
        assert(!no_handle_subtype.has_value());

        if (arg_type != nullptr)
            return CannotBeParameterized(maybe_location);
        if (size == nullptr)
            size = &max_size;

        *out_type = std::make_unique<StringType>(size, nullability);
        return true;
    }

private:
    Size max_size = Size::Max();
};

class HandleTypeTemplate : public TypeTemplate {
public:
    HandleTypeTemplate(Typespace* typespace, ErrorReporter* error_reporter)
        : TypeTemplate(Name(nullptr, "handle"), typespace, error_reporter) {}

    bool Create(const SourceLocation* maybe_location,
                const Type* maybe_arg_type,
                const std::optional<types::HandleSubtype>& handle_subtype,
                const Size* maybe_size,
                types::Nullability nullability,
                std::unique_ptr<Type>* out_type) const {
        assert(maybe_arg_type == nullptr);

        if (maybe_size != nullptr)
            return CannotHaveSize(maybe_location);

        *out_type = std::make_unique<HandleType>(
            handle_subtype.has_value() ?
                handle_subtype.value() :
                types::HandleSubtype::kHandle,
            nullability);
        return true;
    }
};

class RequestTypeTemplate : public TypeTemplate {
public:
    RequestTypeTemplate(Typespace* typespace, ErrorReporter* error_reporter)
        : TypeTemplate(Name(nullptr, "request"), typespace, error_reporter) {}

    bool Create(const SourceLocation* maybe_location,
                const Type* arg_type,
                const std::optional<types::HandleSubtype>& no_handle_subtype,
                const Size* maybe_size,
                types::Nullability nullability,
                std::unique_ptr<Type>* out_type) const {
        assert(!no_handle_subtype.has_value());

        if (arg_type == nullptr)
            return MustBeParameterized(maybe_location);
        if (arg_type->kind != Type::Kind::kIdentifier)
            return Fail(maybe_location, "must be an interface");
        auto interface_type = static_cast<const IdentifierType*>(arg_type);
        if (interface_type->type_decl->kind != Decl::Kind::kInterface)
            return Fail(maybe_location, "must be an interface");
        if (maybe_size != nullptr)
            return CannotHaveSize(maybe_location);

        *out_type = std::make_unique<RequestHandleType>(interface_type, nullability);
        return true;
    }

private:
    // TODO(pascallouis): Make Min/Max an actual value on NumericConstantValue
    // class, to simply write &Size::Max() above.
    Size max_size = Size::Max();
};

class TypeDeclTypeTemplate : public TypeTemplate {
public:
    TypeDeclTypeTemplate(Name name, Typespace* typespace, ErrorReporter* error_reporter,
                         Library* library, TypeDecl* type_decl)
        : TypeTemplate(std::move(name), typespace, error_reporter),
          library_(library), type_decl_(type_decl) {}

    bool Create(const SourceLocation* maybe_location,
                const Type* arg_type,
                const std::optional<types::HandleSubtype>& no_handle_subtype,
                const Size* size,
                types::Nullability nullability,
                std::unique_ptr<Type>* out_type) const {
        assert(!no_handle_subtype.has_value());

        if (!type_decl_->compiled && type_decl_->kind != Decl::Kind::kInterface) {
            if (type_decl_->compiling) {
                type_decl_->recursive = true;
            } else {
                if (!library_->CompileDecl(type_decl_)) {
                    return false;
                }
            }
        }
        auto typeshape = type_decl_->typeshape;
        switch (type_decl_->kind) {
        case Decl::Kind::kInterface:
            typeshape = HandleType::Shape();
            break;

        case Decl::Kind::kXUnion:
            // Do nothing here: nullable XUnions have the same encoding
            // representation as non-optional XUnions (i.e. nullable XUnions are
            // inlined).
            break;

        case Decl::Kind::kTable:
            if (nullability == types::Nullability::kNullable)
                return CannotBeNullable(maybe_location);
            break;

        default:
            if (nullability == types::Nullability::kNullable)
                typeshape = PointerTypeShape(typeshape);
            break;
        }

        *out_type = std::make_unique<IdentifierType>(
            // TODO(FIDL-447): We have to create a copy because IdentifierType
            // has an owned name. Fix this.
            Name(name()->library(), std::string(name()->name_part())),
            nullability, type_decl_, typeshape);
        return true;
    }

private:
    Library* library_;
    TypeDecl* type_decl_;
};

class TypeAliasTypeTemplate : public TypeTemplate {
public:
    TypeAliasTypeTemplate(Name name, Typespace* typespace, ErrorReporter* error_reporter,
                          Library* library,
                          std::unique_ptr<TypeConstructor> partial_type_ctor)
        : TypeTemplate(std::move(name), typespace, error_reporter),
          library_(library), partial_type_ctor_(std::move(partial_type_ctor)) {}

    bool Create(const SourceLocation* maybe_location,
                const Type* maybe_arg_type,
                const std::optional<types::HandleSubtype>& no_handle_subtype,
                const Size* maybe_size,
                types::Nullability maybe_nullability,
                std::unique_ptr<Type>* out_type) const {
        assert(!no_handle_subtype);

        const Type* arg_type = nullptr;
        if (partial_type_ctor_->maybe_arg_type_ctor) {
            if (maybe_arg_type) {
                return Fail(maybe_location, "cannot parametrize twice");
            }
            if (!partial_type_ctor_->maybe_arg_type_ctor->type) {
                if (!library_->CompileTypeConstructor(
                    partial_type_ctor_->maybe_arg_type_ctor.get(),
                    nullptr /* out_typeshape */))
                    return false;
            }
            arg_type = partial_type_ctor_->maybe_arg_type_ctor->type;
        } else {
            arg_type = maybe_arg_type;
        }

        const Size* size = nullptr;
        if (partial_type_ctor_->maybe_size) {
            if (maybe_size) {
                return Fail(maybe_location, "cannot bound twice");
            }
            if (!library_->ResolveConstant(partial_type_ctor_->maybe_size.get(), &library_->kSizeType))
                return Fail(maybe_location, "unable to parse size bound");
            size = static_cast<const Size*>(&partial_type_ctor_->maybe_size->Value());
        } else {
            size = maybe_size;
        }

        types::Nullability nullability;
        if (partial_type_ctor_->nullability == types::Nullability::kNullable) {
            if (maybe_nullability == types::Nullability::kNullable) {
                return Fail(maybe_location, "cannot indicate nullability twice");
            }
            nullability = types::Nullability::kNullable;
        } else {
            nullability = maybe_nullability;
        }

        return typespace_->CreateNotOwned(partial_type_ctor_->name, arg_type,
                                          std::optional<types::HandleSubtype>(),
                                          size, nullability, out_type);
    }

private:
    Library* library_;
    std::unique_ptr<TypeConstructor> partial_type_ctor_;
};

Typespace Typespace::RootTypes(ErrorReporter* error_reporter) {
    Typespace root_typespace(error_reporter);

    auto add_template = [&](std::unique_ptr<TypeTemplate> type_template) {
        auto name = type_template->name();
        root_typespace.templates_.emplace(name, std::move(type_template));
    };

    auto add_primitive = [&](const std::string& name, types::PrimitiveSubtype subtype) {
        add_template(std::make_unique<PrimitiveTypeTemplate>(
            &root_typespace, error_reporter, name, subtype));
    };

    add_primitive("bool", types::PrimitiveSubtype::kBool);

    add_primitive("int8", types::PrimitiveSubtype::kInt8);
    add_primitive("int16", types::PrimitiveSubtype::kInt16);
    add_primitive("int32", types::PrimitiveSubtype::kInt32);
    add_primitive("int64", types::PrimitiveSubtype::kInt64);
    add_primitive("uint8", types::PrimitiveSubtype::kUint8);
    add_primitive("uint16", types::PrimitiveSubtype::kUint16);
    add_primitive("uint32", types::PrimitiveSubtype::kUint32);
    add_primitive("uint64", types::PrimitiveSubtype::kUint64);

    add_primitive("float32", types::PrimitiveSubtype::kFloat32);
    add_primitive("float64", types::PrimitiveSubtype::kFloat64);

    // TODO(FIDL-483): Remove when there is generalized support.
    add_primitive("byte", types::PrimitiveSubtype::kUint8);
    add_template(std::make_unique<BytesTypeTemplate>(
        &root_typespace, error_reporter));

    add_template(std::make_unique<ArrayTypeTemplate>(
        &root_typespace, error_reporter));
    add_template(std::make_unique<VectorTypeTemplate>(
        &root_typespace, error_reporter));
    add_template(std::make_unique<StringTypeTemplate>(
        &root_typespace, error_reporter));
    add_template(std::make_unique<HandleTypeTemplate>(
        &root_typespace, error_reporter));
    add_template(std::make_unique<RequestTypeTemplate>(
        &root_typespace, error_reporter));

    return root_typespace;
}

AttributeSchema::AttributeSchema(const std::set<Placement>& allowed_placements,
                                 const std::set<std::string> allowed_values,
                                 Constraint constraint)
    : allowed_placements_(allowed_placements),
      allowed_values_(allowed_values),
      constraint_(std::move(constraint)) {}

void AttributeSchema::ValidatePlacement(ErrorReporter* error_reporter,
                                        const raw::Attribute& attribute,
                                        Placement placement) const {
    if (allowed_placements_.size() == 0)
        return;
    auto iter = allowed_placements_.find(placement);
    if (iter != allowed_placements_.end())
        return;
    std::string message("placement of attribute '");
    message.append(attribute.name);
    message.append("' disallowed here");
    error_reporter->ReportError(attribute.location(), message);
}

void AttributeSchema::ValidateValue(ErrorReporter* error_reporter,
                                    const raw::Attribute& attribute) const {
    if (allowed_values_.size() == 0)
        return;
    auto iter = allowed_values_.find(attribute.value);
    if (iter != allowed_values_.end())
        return;
    std::string message("attribute '");
    message.append(attribute.name);
    message.append("' has invalid value '");
    message.append(attribute.value);
    message.append("', should be one of '");
    bool first = true;
    for (const auto& hint : allowed_values_) {
        if (!first)
            message.append(", ");
        message.append(hint);
        message.append("'");
        first = false;
    }
    error_reporter->ReportError(attribute.location(), message);
}

void AttributeSchema::ValidateConstraint(ErrorReporter* error_reporter,
                                         const raw::Attribute& attribute,
                                         const Decl* decl) const {
    auto check = error_reporter->Checkpoint();
    auto passed = constraint_(error_reporter, attribute, decl);
    if (passed) {
        assert(check.NoNewErrors() && "cannot add errors and pass");
    } else if (check.NoNewErrors()) {
        std::string message("declaration did not satisfy constraint of attribute '");
        message.append(attribute.name);
        message.append("' with value '");
        message.append(attribute.value);
        message.append("'");
        // TODO(pascallouis): It would be nicer to use the location of
        // the declaration, however we do not keep it around today.
        error_reporter->ReportError(attribute.location(), message);
    }
}

bool SimpleLayoutConstraint(ErrorReporter* error_reporter,
                            const raw::Attribute& attribute,
                            const Decl* decl) {
    assert(decl->kind == Decl::Kind::kStruct);
    auto struct_decl = static_cast<const Struct*>(decl);
    bool ok = true;
    for (const auto& member : struct_decl->members) {
        if (!IsSimple(member.type_ctor.get()->type, member.fieldshape)) {
            std::string message("member '");
            message.append(member.name.data());
            message.append("' is not simple");
            error_reporter->ReportError(member.name, message);
            ok = false;
        }
    }
    return ok;
}

bool ParseBound(ErrorReporter* error_reporter, const SourceLocation& location,
                const std::string& input, uint32_t* out_value) {
    auto result = utils::ParseNumeric(input, out_value, 10);
    switch (result) {
    case utils::ParseNumericResult::kOutOfBounds:
        error_reporter->ReportError(location, "bound is too big");
        return false;
    case utils::ParseNumericResult::kMalformed: {
        std::string message("unable to parse bound '");
        message.append(input);
        message.append("'");
        error_reporter->ReportError(location, message);
        return false;
    }
    case utils::ParseNumericResult::kSuccess:
        return true;
    }
}

bool MaxBytesConstraint(ErrorReporter* error_reporter,
                        const raw::Attribute& attribute,
                        const Decl* decl) {
    uint32_t bound;
    if (!ParseBound(error_reporter, attribute.location(), attribute.value, &bound))
        return false;
    uint32_t max_bytes = std::numeric_limits<uint32_t>::max();
    switch (decl->kind) {
    case Decl::Kind::kStruct: {
        auto struct_decl = static_cast<const Struct*>(decl);
        max_bytes = struct_decl->typeshape.Size() + struct_decl->typeshape.MaxOutOfLine();
        break;
    }
    case Decl::Kind::kTable: {
        auto table_decl = static_cast<const Table*>(decl);
        max_bytes = table_decl->typeshape.Size() + table_decl->typeshape.MaxOutOfLine();
        break;
    }
    case Decl::Kind::kUnion: {
        auto union_decl = static_cast<const Union*>(decl);
        max_bytes = union_decl->typeshape.Size() + union_decl->typeshape.MaxOutOfLine();
        break;
    }
    case Decl::Kind::kXUnion: {
        auto xunion_decl = static_cast<const XUnion*>(decl);
        max_bytes = xunion_decl->typeshape.Size() + xunion_decl->typeshape.MaxOutOfLine();
        break;
    }
    default:
        assert(false && "unexpected kind");
        return false;
    }
    if (max_bytes > bound) {
        std::ostringstream message;
        message << "too large: only ";
        message << bound;
        message << " bytes allowed, but ";
        message << max_bytes;
        message << " bytes found";
        error_reporter->ReportError(attribute.location(), message.str());
        return false;
    }
    return true;
}

bool MaxHandlesConstraint(ErrorReporter* error_reporter,
                          const raw::Attribute& attribute,
                          const Decl* decl) {
    uint32_t bound;
    if (!ParseBound(error_reporter, attribute.location(), attribute.value.c_str(), &bound))
        return false;
    uint32_t max_handles = std::numeric_limits<uint32_t>::max();
    switch (decl->kind) {
    case Decl::Kind::kStruct: {
        auto struct_decl = static_cast<const Struct*>(decl);
        max_handles = struct_decl->typeshape.MaxHandles();
        break;
    }
    case Decl::Kind::kTable: {
        auto table_decl = static_cast<const Table*>(decl);
        max_handles = table_decl->typeshape.MaxHandles();
        break;
    }
    case Decl::Kind::kUnion: {
        auto union_decl = static_cast<const Union*>(decl);
        max_handles = union_decl->typeshape.MaxHandles();
        break;
    }
    case Decl::Kind::kXUnion: {
        auto xunion_decl = static_cast<const XUnion*>(decl);
        max_handles = xunion_decl->typeshape.MaxHandles();
        break;
    }
    default:
        assert(false && "unexpected kind");
        return false;
    }
    if (max_handles > bound) {
        std::ostringstream message;
        message << "too many handles: only ";
        message << bound;
        message << " allowed, but ";
        message << max_handles;
        message << " found";
        error_reporter->ReportError(attribute.location(), message.str());
        return false;
    }
    return true;
}

bool ResultShapeConstraint(ErrorReporter* error_reporter,
                           const raw::Attribute& attribute,
                           const Decl* decl) {
    assert(decl->kind == Decl::Kind::kUnion);
    auto union_decl = static_cast<const Union*>(decl);
    assert(union_decl->members.size() == 2);
    auto error_type = union_decl->members.at(1).type_ctor->type;

    const PrimitiveType* error_primitive = nullptr;
    if (error_type->kind == Type::Kind::kPrimitive) {
        error_primitive = static_cast<const PrimitiveType*>(error_type);
    } else if (error_type->kind == Type::Kind::kIdentifier) {
        auto identifier_type = static_cast<const IdentifierType*>(error_type);
        if (identifier_type->type_decl->kind == Decl::Kind::kEnum) {
            auto error_enum = static_cast<const Enum*>(identifier_type->type_decl);
            assert(error_enum->subtype_ctor->type->kind == Type::Kind::kPrimitive);
            error_primitive = static_cast<const PrimitiveType*>(error_enum->subtype_ctor->type);
        }
    }

    if (!error_primitive ||
        (error_primitive->subtype != types::PrimitiveSubtype::kInt32 &&
         error_primitive->subtype != types::PrimitiveSubtype::kUint32)) {
        error_reporter->ReportError(
            decl->name.maybe_location(),
            "invalid error type: must be int32, uint32 or an enum therof");
        return false;
    }

    return true;
}

static std::string Trim(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
                return !std::isspace(ch);
            }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
                return !std::isspace(ch);
            })
                .base(),
            s.end());
    return s;
}

bool TransportConstraint(ErrorReporter* error_reporter,
                         const raw::Attribute& attribute,
                         const Decl* decl) {
    // Parse comma separated transports
    const std::string& value = attribute.value;
    std::string::size_type prev_pos = 0;
    std::string::size_type pos;
    std::vector<std::string> transports;
    while ((pos = value.find(',', prev_pos)) != std::string::npos) {
        transports.emplace_back(Trim(value.substr(prev_pos, pos - prev_pos)));
        prev_pos = pos + 1;
    }
    transports.emplace_back(Trim(value.substr(prev_pos)));

    // Validate that they're ok
    static const std::set<std::string> kValidTransports = {
        "Channel",
        "SocketControl",
        "OvernetEmbedded",
        "OvernetInternal",
    };
    for (auto transport : transports) {
        if (kValidTransports.count(transport) == 0) {
            std::ostringstream out;
            out << "invalid transport type: got " << transport << " expected one of ";
            bool first = true;
            for (const auto& t : kValidTransports) {
                if (!first) {
                    out << ", ";
                }
                first = false;
                out << t;
            }
            error_reporter->ReportError(decl->name.maybe_location(), out.str());
            return false;
        }
    }
    return true;
}

Libraries::Libraries() {
    // clang-format off
    AddAttributeSchema("Discoverable", AttributeSchema({
        AttributeSchema::Placement::kInterfaceDecl,
    }, {
        "",
    }));
    AddAttributeSchema("Doc", AttributeSchema({
        /* any placement */
    }, {
        /* any value */
    }));
    AddAttributeSchema("FragileBase", AttributeSchema({
        AttributeSchema::Placement::kInterfaceDecl,
    }, {
        "",
    }));
    AddAttributeSchema("Layout", AttributeSchema({
        AttributeSchema::Placement::kInterfaceDecl,
    }, {
        "Simple",
    },
    SimpleLayoutConstraint));
    AddAttributeSchema("MaxBytes", AttributeSchema({
        AttributeSchema::Placement::kInterfaceDecl,
        AttributeSchema::Placement::kMethod,
        AttributeSchema::Placement::kStructDecl,
        AttributeSchema::Placement::kTableDecl,
        AttributeSchema::Placement::kUnionDecl,
        AttributeSchema::Placement::kXUnionDecl,
    }, {
        /* any value */
    },
    MaxBytesConstraint));
    AddAttributeSchema("MaxHandles", AttributeSchema({
        AttributeSchema::Placement::kInterfaceDecl,
        AttributeSchema::Placement::kMethod,
        AttributeSchema::Placement::kStructDecl,
        AttributeSchema::Placement::kTableDecl,
        AttributeSchema::Placement::kUnionDecl,
        AttributeSchema::Placement::kXUnionDecl,
    }, {
        /* any value */
    },
    MaxHandlesConstraint));
    AddAttributeSchema("Result", AttributeSchema({
        AttributeSchema::Placement::kUnionDecl,
    }, {
        "",
    },
    ResultShapeConstraint));
    AddAttributeSchema("Selector", AttributeSchema({
        AttributeSchema::Placement::kMethod,
        AttributeSchema::Placement::kXUnionMember,
    }, {
        /* any value */
    }));
    AddAttributeSchema("Transport", AttributeSchema({
        AttributeSchema::Placement::kInterfaceDecl,
    }, {
        /* any value */
    }, TransportConstraint));
    // clang-format on
}

bool Libraries::Insert(std::unique_ptr<Library> library) {
    std::vector<fidl::StringView> library_name = library->name();
    auto iter = all_libraries_.emplace(library_name, std::move(library));
    return iter.second;
}

bool Libraries::Lookup(const std::vector<StringView>& library_name,
                       Library** out_library) const {
    auto iter = all_libraries_.find(library_name);
    if (iter == all_libraries_.end()) {
        return false;
    }

    *out_library = iter->second.get();
    return true;
}

size_t EditDistance(const std::string& sequence1, const std::string& sequence2) {
    size_t s1_length = sequence1.length();
    size_t s2_length = sequence2.length();
    size_t row1[s1_length + 1];
    size_t row2[s1_length + 1];
    size_t* last_row = row1;
    size_t* this_row = row2;
    for (size_t i = 0; i <= s1_length; i++)
        last_row[i] = i;
    for (size_t j = 0; j < s2_length; j++) {
        this_row[0] = j + 1;
        auto s2c = sequence2[j];
        for (size_t i = 1; i <= s1_length; i++) {
            auto s1c = sequence1[i - 1];
            this_row[i] = std::min(std::min(
                                       last_row[i] + 1, this_row[i - 1] + 1),
                                   last_row[i - 1] + (s1c == s2c ? 0 : 1));
        }
        std::swap(last_row, this_row);
    }
    return last_row[s1_length];
}

const AttributeSchema* Libraries::RetrieveAttributeSchema(ErrorReporter* error_reporter,
                                                          const raw::Attribute& attribute) const {
    const auto& attribute_name = attribute.name;
    auto iter = attribute_schemas_.find(attribute_name);
    if (iter != attribute_schemas_.end()) {
        const auto& schema = iter->second;
        return &schema;
    }

    // Skip typo check?
    if (error_reporter == nullptr)
        return nullptr;

    // Match against all known attributes.
    for (const auto& name_and_schema : attribute_schemas_) {
        auto edit_distance = EditDistance(name_and_schema.first, attribute_name);
        if (0 < edit_distance && edit_distance < 2) {
            std::string message("suspect attribute with name '");
            message.append(attribute_name);
            message.append("'; did you mean '");
            message.append(name_and_schema.first);
            message.append("'?");
            error_reporter->ReportWarning(attribute.location(), message);
            return nullptr;
        }
    }

    return nullptr;
}

bool Dependencies::Register(StringView filename, Library* dep_library,
                            const std::unique_ptr<raw::Identifier>& maybe_alias) {
    auto library_name = dep_library->name();
    if (!InsertByName(filename, library_name, dep_library)) {
        return false;
    }

    if (maybe_alias) {
        std::vector<StringView> alias_name = {maybe_alias->location().data()};
        if (!InsertByName(filename, alias_name, dep_library)) {
            return false;
        }
    }

    dependencies_aggregate_.insert(dep_library);

    return true;
}

bool Dependencies::InsertByName(StringView filename, const std::vector<StringView>& name,
                                Library* library) {
    auto iter = dependencies_.find(filename);
    if (iter == dependencies_.end()) {
        dependencies_.emplace(filename, std::make_unique<ByName>());
    }

    iter = dependencies_.find(filename);
    assert(iter != dependencies_.end());

    auto insert = iter->second->emplace(name, library);
    return insert.second;
}

bool Dependencies::Lookup(StringView filename, const std::vector<StringView>& name,
                          Library** out_library) {
    auto iter1 = dependencies_.find(filename);
    if (iter1 == dependencies_.end()) {
        return false;
    }

    auto iter2 = iter1->second->find(name);
    if (iter2 == iter1->second->end()) {
        return false;
    }

    *out_library = iter2->second;
    return true;
}

// Consuming the AST is primarily concerned with walking the tree and
// flattening the representation. The AST's declaration nodes are
// converted into the Library's foo_declaration structures. This means pulling
// a struct declaration inside an interface out to the top level and
// so on.

std::string LibraryName(const Library* library, StringView separator) {
    if (library != nullptr) {
        return StringJoin(library->name(), separator);
    }
    return std::string();
}

bool Library::Fail(StringView message) {
    error_reporter_->ReportError(message);
    return false;
}

bool Library::Fail(const SourceLocation* maybe_location, StringView message) {
    error_reporter_->ReportError(maybe_location, message);
    return false;
}

void Library::ValidateAttributesPlacement(AttributeSchema::Placement placement,
                                          const raw::AttributeList* attributes) {
    if (attributes == nullptr)
        return;
    for (const auto& attribute : attributes->attributes) {
        auto schema = all_libraries_->RetrieveAttributeSchema(error_reporter_, attribute);
        if (schema != nullptr) {
            schema->ValidatePlacement(error_reporter_, attribute, placement);
            schema->ValidateValue(error_reporter_, attribute);
        }
    }
}

void Library::ValidateAttributesConstraints(const Decl* decl,
                                            const raw::AttributeList* attributes) {
    if (attributes == nullptr)
        return;
    for (const auto& attribute : attributes->attributes) {
        auto schema = all_libraries_->RetrieveAttributeSchema(nullptr, attribute);
        if (schema != nullptr)
            schema->ValidateConstraint(error_reporter_, attribute, decl);
    }
}

SourceLocation Library::GeneratedSimpleName(const std::string& name) {
    return generated_source_file_.AddLine(name);
}

Name Library::NextAnonymousName() {
    // TODO(FIDL-596): Improve anonymous name generation. We want to be
    // specific about how these names are generated once they appear in the
    // JSON IR, and are exposed to the backends.
    std::ostringstream data;
    data << "SomeLongAnonymousPrefix";
    data << anon_counter_++;
    return Name(this, GeneratedSimpleName(data.str()));
}

Name Library::DerivedName(const std::vector<StringView>& components) {
    return Name(this, GeneratedSimpleName(StringJoin(components, "_")));
}

bool Library::CompileCompoundIdentifier(const raw::CompoundIdentifier* compound_identifier,
                                        SourceLocation location, Name* name_out) {
    const auto& components = compound_identifier->components;
    assert(components.size() >= 1);

    SourceLocation decl_name = components.back()->location();

    if (components.size() == 1) {
        *name_out = Name(this, decl_name);
        return true;
    }

    std::vector<StringView> library_name;
    for (auto iter = components.begin();
         iter != components.end() - 1;
         ++iter) {
        library_name.push_back((*iter)->location().data());
    }

    auto filename = location.source_file().filename();
    Library* dep_library = nullptr;
    if (!dependencies_.Lookup(filename, library_name, &dep_library)) {
        std::string message("Unknown dependent library ");
        message += NameLibrary(library_name);
        message += ". Did you require it with `using`?";
        const auto& location = components[0]->location();
        return Fail(location, message);
    }

    // Resolve the name.
    *name_out = Name(dep_library, decl_name);
    return true;
}

void Library::RegisterConst(Const* decl) {
    const Name* name = &decl->name;
    constants_.emplace(name, decl);
}

bool Library::RegisterDecl(Decl* decl) {
    const Name* name = &decl->name;
    auto iter = declarations_.emplace(name, decl);
    if (!iter.second) {
        std::string message = "Name collision: ";
        message.append(name->name_part());
        return Fail(*name, message);
    }
    switch (decl->kind) {
    case Decl::Kind::kBits:
    case Decl::Kind::kEnum:
    case Decl::Kind::kStruct:
    case Decl::Kind::kTable:
    case Decl::Kind::kUnion:
    case Decl::Kind::kXUnion:
    case Decl::Kind::kInterface: {
        auto type_decl = static_cast<TypeDecl*>(decl);
        auto type_template = std::make_unique<TypeDeclTypeTemplate>(
            Name(name->library(), std::string(name->name_part())),
            typespace_, error_reporter_, this, type_decl);
        typespace_->AddTemplate(std::move(type_template));
        break;
    }
    default:
        assert(decl->kind == Decl::Kind::kConst);
    }
    return true;
}

bool Library::ConsumeConstant(std::unique_ptr<raw::Constant> raw_constant, SourceLocation location,
                              std::unique_ptr<Constant>* out_constant) {
    switch (raw_constant->kind) {
    case raw::Constant::Kind::kIdentifier: {
        auto identifier = static_cast<raw::IdentifierConstant*>(raw_constant.get());
        Name name;
        if (!CompileCompoundIdentifier(identifier->identifier.get(), location, &name)) {
            return false;
        }
        *out_constant = std::make_unique<IdentifierConstant>(std::move(name));
        break;
    }
    case raw::Constant::Kind::kLiteral: {
        auto literal = static_cast<raw::LiteralConstant*>(raw_constant.get());
        *out_constant = std::make_unique<LiteralConstant>(std::move(literal->literal));
        break;
    }
    }
    return true;
}

bool Library::ConsumeTypeConstructor(std::unique_ptr<raw::TypeConstructor> raw_type_ctor,
                                     SourceLocation location,
                                     std::unique_ptr<TypeConstructor>* out_type_ctor) {
    Name name;
    if (!CompileCompoundIdentifier(raw_type_ctor->identifier.get(), location, &name))
        return false;

    std::unique_ptr<TypeConstructor> maybe_arg_type_ctor;
    if (raw_type_ctor->maybe_arg_type_ctor != nullptr) {
        if (!ConsumeTypeConstructor(std::move(raw_type_ctor->maybe_arg_type_ctor), location, &maybe_arg_type_ctor))
            return false;
    }

    std::unique_ptr<Constant> maybe_size;
    if (raw_type_ctor->maybe_size != nullptr) {
        if (!ConsumeConstant(std::move(raw_type_ctor->maybe_size), location, &maybe_size))
            return false;
    }

    *out_type_ctor = std::make_unique<TypeConstructor>(
        std::move(name),
        std::move(maybe_arg_type_ctor),
        raw_type_ctor->handle_subtype,
        std::move(maybe_size),
        raw_type_ctor->nullability);
    return true;
}

bool Library::ConsumeUsing(std::unique_ptr<raw::Using> using_directive) {
    if (using_directive->maybe_type_ctor)
        return ConsumeTypeAlias(std::move(using_directive));

    std::vector<StringView> library_name;
    for (const auto& component : using_directive->using_path->components) {
        library_name.push_back(component->location().data());
    }

    Library* dep_library = nullptr;
    if (!all_libraries_->Lookup(library_name, &dep_library)) {
        std::string message("Could not find library named ");
        message += NameLibrary(library_name);
        message += ". Did you include its sources with --files?";
        const auto& location = using_directive->using_path->components[0]->location();
        return Fail(location, message);
    }

    auto filename = using_directive->location().source_file().filename();
    if (!dependencies_.Register(filename, dep_library, using_directive->maybe_alias)) {
        std::string message("Library ");
        message += NameLibrary(library_name);
        message += " already imported. Did you require it twice?";
        return Fail(message);
    }

    // Import declarations, and type aliases of dependent library.
    const auto& declarations = dep_library->declarations_;
    declarations_.insert(declarations.begin(), declarations.end());
    return true;
}

bool Library::ConsumeTypeAlias(std::unique_ptr<raw::Using> using_directive) {
    assert(using_directive->maybe_type_ctor);

    auto location = using_directive->using_path->components[0]->location();
    auto alias_name = Name(this, location);
    std::unique_ptr<TypeConstructor> partial_type_ctor_;
    if (!ConsumeTypeConstructor(std::move(using_directive->maybe_type_ctor), location,
                                &partial_type_ctor_))
        return false;
    typespace_->AddTemplate(std::make_unique<TypeAliasTypeTemplate>(
        std::move(alias_name), typespace_, error_reporter_, this, std::move(partial_type_ctor_)));
    return true;
}

bool Library::ConsumeBitsDeclaration(std::unique_ptr<raw::BitsDeclaration> bits_declaration) {
    std::vector<Bits::Member> members;
    for (auto& member : bits_declaration->members) {
        auto location = member->identifier->location();
        std::unique_ptr<Constant> value;
        if (!ConsumeConstant(std::move(member->value), location, &value))
            return false;
        members.emplace_back(location, std::move(value), std::move(member->attributes));
        // TODO(pascallouis): right now, members are not registered. Look into
        // registering them, potentially under the bits name qualifier such as
        // <name_of_bits>.<name_of_member>.
    }

    std::unique_ptr<TypeConstructor> type_ctor;
    if (bits_declaration->maybe_type_ctor) {
        if (!ConsumeTypeConstructor(std::move(bits_declaration->maybe_type_ctor),
                                    bits_declaration->location(), &type_ctor))
            return false;
    } else {
        type_ctor = std::make_unique<TypeConstructor>(
            Name(nullptr, "uint32"),
            nullptr /* maybe_arg_type */,
            std::optional<types::HandleSubtype>(),
            nullptr /* maybe_size */,
            types::Nullability::kNonnullable);
    }

    bits_declarations_.push_back(std::make_unique<Bits>(
        std::move(bits_declaration->attributes),
        Name(this, bits_declaration->identifier->location()),
        std::move(type_ctor),
        std::move(members)));
    if (!RegisterDecl(bits_declarations_.back().get()))
        return false;

    return true;
}

bool Library::ConsumeConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const_declaration) {
    auto attributes = std::move(const_declaration->attributes);
    auto location = const_declaration->identifier->location();
    auto name = Name(this, location);
    std::unique_ptr<TypeConstructor> type_ctor;
    if (!ConsumeTypeConstructor(std::move(const_declaration->type_ctor), location, &type_ctor))
        return false;

    std::unique_ptr<Constant> constant;
    if (!ConsumeConstant(std::move(const_declaration->constant), location, &constant))
        return false;

    const_declarations_.push_back(std::make_unique<Const>(std::move(attributes), std::move(name),
                                                          std::move(type_ctor), std::move(constant)));
    auto decl = const_declarations_.back().get();
    RegisterConst(decl);
    return RegisterDecl(decl);
}

bool Library::ConsumeEnumDeclaration(std::unique_ptr<raw::EnumDeclaration> enum_declaration) {
    std::vector<Enum::Member> members;
    for (auto& member : enum_declaration->members) {
        auto location = member->identifier->location();
        std::unique_ptr<Constant> value;
        if (!ConsumeConstant(std::move(member->value), location, &value))
            return false;
        members.emplace_back(location, std::move(value), std::move(member->attributes));
        // TODO(pascallouis): right now, members are not registered. Look into
        // registering them, potentially under the enum name qualifier such as
        // <name_of_enum>.<name_of_member>.
    }

    std::unique_ptr<TypeConstructor> type_ctor;
    if (enum_declaration->maybe_type_ctor) {
        if (!ConsumeTypeConstructor(std::move(enum_declaration->maybe_type_ctor),
                                    enum_declaration->location(), &type_ctor))
            return false;
    } else {
        type_ctor = std::make_unique<TypeConstructor>(
            Name(nullptr, "uint32"),
            nullptr /* maybe_arg_type */,
            std::optional<types::HandleSubtype>(),
            nullptr /* maybe_size */,
            types::Nullability::kNonnullable);
    }

    enum_declarations_.push_back(std::make_unique<Enum>(
        std::move(enum_declaration->attributes),
        Name(this, enum_declaration->identifier->location()),
        std::move(type_ctor),
        std::move(members)));
    if (!RegisterDecl(enum_declarations_.back().get()))
        return false;

    return true;
}

bool Library::CreateMethodResult(const Name& interface_name,
                                 raw::InterfaceMethod* method,
                                 Struct* in_response,
                                 Struct** out_response) {
    // Compile the error type.
    auto error_location = method->maybe_error_ctor->location();
    std::unique_ptr<TypeConstructor> error_type_ctor;
    if (!ConsumeTypeConstructor(std::move(method->maybe_error_ctor), error_location, &error_type_ctor))
        return false;

    // Make the Result union containing the response struct and the
    // error type.
    Union::Member response_member{
        IdentifierTypeForDecl(in_response, types::Nullability::kNonnullable),
        GeneratedSimpleName("response"),
        nullptr};
    Union::Member error_member{
        std::move(error_type_ctor),
        GeneratedSimpleName("err"),
        nullptr};
    SourceLocation method_name = method->identifier->location();
    Name result_name = DerivedName({interface_name.name_part(), method_name.data(), "Result"});
    std::vector<Union::Member> result_members;
    result_members.push_back(std::move(response_member));
    result_members.push_back(std::move(error_member));
    std::vector<raw::Attribute> result_attributes;
    result_attributes.emplace_back(*method, "Result", "");
    auto result_attributelist = std::make_unique<raw::AttributeList>(*method,
                                                                     std::move(result_attributes));
    union_declarations_.push_back(std::make_unique<Union>(std::move(result_attributelist),
                                                          std::move(result_name),
                                                          std::move(result_members)));
    auto result_decl = union_declarations_.back().get();
    if (!RegisterDecl(result_decl))
        return false;

    // Make a new response struct for the method containing just the
    // result union.
    std::vector<Struct::Member> response_members;
    response_members.push_back(Struct::Member(IdentifierTypeForDecl(result_decl, types::Nullability::kNonnullable),
                                              GeneratedSimpleName("result"), nullptr, nullptr));
    struct_declarations_.push_back(std::make_unique<Struct>(nullptr, NextAnonymousName(), std::move(response_members), true));
    *out_response = struct_declarations_.back().get();
    if (!RegisterDecl(*out_response))
        return false;

    return true;
}

bool Library::ConsumeInterfaceDeclaration(
    std::unique_ptr<raw::InterfaceDeclaration> interface_declaration) {
    auto attributes = std::move(interface_declaration->attributes);
    auto name = Name(this, interface_declaration->identifier->location());

    std::set<Name> superinterfaces;
    for (auto& superinterface : interface_declaration->superinterfaces) {
        auto& protocol_name = superinterface->protocol_name;
        auto location = protocol_name->components[0]->location();
        Name superinterface_name;
        if (!CompileCompoundIdentifier(protocol_name.get(), location, &superinterface_name)) {
            return false;
        }
        if (!superinterfaces.insert(std::move(superinterface_name)).second)
            return Fail(superinterface_name, "protocol composed multiple times");
    }

    std::vector<Interface::Method> methods;
    for (auto& method : interface_declaration->methods) {
        std::unique_ptr<raw::Ordinal> ordinal_literal =
            std::make_unique<raw::Ordinal>(fidl::ordinals::GetOrdinal(library_name_, name.name_part(), *method));
        std::unique_ptr<raw::Ordinal> generated_ordinal =
            std::make_unique<raw::Ordinal>(fidl::ordinals::GetGeneratedOrdinal(library_name_, name.name_part(), *method));
        auto attributes = std::move(method->attributes);
        SourceLocation method_name = method->identifier->location();

        Struct* maybe_request = nullptr;
        if (method->maybe_request != nullptr) {
            Name request_name = NextAnonymousName();
            if (!ConsumeParameterList(std::move(request_name), std::move(method->maybe_request), true, &maybe_request))
                return false;
        }

        bool has_error = (method->maybe_error_ctor != nullptr);

        Struct* maybe_response = nullptr;
        if (method->maybe_response != nullptr) {
            Name response_name = has_error ? DerivedName({name.name_part(), method_name.data(), "Response"}) : NextAnonymousName();
            if (!ConsumeParameterList(std::move(response_name), std::move(method->maybe_response), !has_error, &maybe_response))
                return false;
        }

        if (has_error) {
            if (!CreateMethodResult(name, method.get(), maybe_response, &maybe_response))
                return false;
        }

        assert(maybe_request != nullptr || maybe_response != nullptr);
        methods.emplace_back(std::move(attributes),
                             std::move(generated_ordinal),
                             std::move(ordinal_literal),
                             std::move(method_name), std::move(maybe_request),
                             std::move(maybe_response));
    }

    interface_declarations_.push_back(
        std::make_unique<Interface>(std::move(attributes), std::move(name),
                                    std::move(superinterfaces), std::move(methods)));
    return RegisterDecl(interface_declarations_.back().get());
}

std::unique_ptr<TypeConstructor> Library::IdentifierTypeForDecl(const Decl* decl, types::Nullability nullability) {
    return std::make_unique<TypeConstructor>(
        Name(decl->name.library(), decl->name.name_part()),
        nullptr /* maybe_arg_type */,
        std::optional<types::HandleSubtype>(),
        nullptr /* maybe_size */,
        nullability);
}

bool Library::ConsumeParameterList(Name name, std::unique_ptr<raw::ParameterList> parameter_list,
                                   bool anonymous, Struct** out_struct_decl) {
    std::vector<Struct::Member> members;
    for (auto& parameter : parameter_list->parameter_list) {
        const SourceLocation name = parameter->identifier->location();
        std::unique_ptr<TypeConstructor> type_ctor;
        if (!ConsumeTypeConstructor(std::move(parameter->type_ctor), name, &type_ctor))
            return false;
        members.emplace_back(
            std::move(type_ctor), name,
            nullptr /* maybe_default_value */,
            nullptr /* attributes */);
    }

    struct_declarations_.push_back(
        std::make_unique<Struct>(nullptr /* attributes */, std::move(name), std::move(members),
                                 anonymous));

    auto struct_decl = struct_declarations_.back().get();
    if (!RegisterDecl(struct_decl))
        return false;

    *out_struct_decl = struct_decl;
    return true;
}

bool Library::ConsumeStructDeclaration(std::unique_ptr<raw::StructDeclaration> struct_declaration) {
    auto attributes = std::move(struct_declaration->attributes);
    auto name = Name(this, struct_declaration->identifier->location());

    std::vector<Struct::Member> members;
    for (auto& member : struct_declaration->members) {
        std::unique_ptr<TypeConstructor> type_ctor;
        auto location = member->identifier->location();
        if (!ConsumeTypeConstructor(std::move(member->type_ctor), location, &type_ctor))
            return false;
        std::unique_ptr<Constant> maybe_default_value;
        if (member->maybe_default_value != nullptr) {
            if (!ConsumeConstant(std::move(member->maybe_default_value), location,
                                 &maybe_default_value))
                return false;
        }
        auto attributes = std::move(member->attributes);
        members.emplace_back(std::move(type_ctor), member->identifier->location(),
                             std::move(maybe_default_value), std::move(attributes));
    }

    struct_declarations_.push_back(
        std::make_unique<Struct>(std::move(attributes), std::move(name), std::move(members)));
    return RegisterDecl(struct_declarations_.back().get());
}

bool Library::ConsumeTableDeclaration(std::unique_ptr<raw::TableDeclaration> table_declaration) {
    auto attributes = std::move(table_declaration->attributes);
    auto name = Name(this, table_declaration->identifier->location());

    std::vector<Table::Member> members;
    for (auto& member : table_declaration->members) {
        auto ordinal_literal = std::move(member->ordinal);

        if (member->maybe_used) {
            std::unique_ptr<TypeConstructor> type_ctor;
            if (!ConsumeTypeConstructor(std::move(member->maybe_used->type_ctor), member->location(), &type_ctor))
                return false;
            std::unique_ptr<Constant> maybe_default_value;
            if (member->maybe_used->maybe_default_value != nullptr) {
                if (!ConsumeConstant(std::move(member->maybe_used->maybe_default_value),
                                     member->location(), &maybe_default_value))
                    return false;
            }
            if (type_ctor->nullability != types::Nullability::kNonnullable) {
                return Fail(member->location(), "Table members cannot be nullable");
            }
            auto attributes = std::move(member->maybe_used->attributes);
            members.emplace_back(std::move(ordinal_literal), std::move(type_ctor),
                                 member->maybe_used->identifier->location(),
                                 std::move(maybe_default_value), std::move(attributes));
        } else {
            members.emplace_back(std::move(ordinal_literal), member->location());
        }
    }

    table_declarations_.push_back(
        std::make_unique<Table>(std::move(attributes), std::move(name), std::move(members)));
    return RegisterDecl(table_declarations_.back().get());
}

bool Library::ConsumeUnionDeclaration(std::unique_ptr<raw::UnionDeclaration> union_declaration) {
    std::vector<Union::Member> members;
    for (auto& member : union_declaration->members) {
        auto location = member->identifier->location();
        std::unique_ptr<TypeConstructor> type_ctor;
        if (!ConsumeTypeConstructor(std::move(member->type_ctor), location, &type_ctor))
            return false;
        auto attributes = std::move(member->attributes);
        members.emplace_back(std::move(type_ctor), location, std::move(attributes));
    }

    auto attributes = std::move(union_declaration->attributes);
    auto name = Name(this, union_declaration->identifier->location());

    union_declarations_.push_back(
        std::make_unique<Union>(std::move(attributes), std::move(name), std::move(members)));
    return RegisterDecl(union_declarations_.back().get());
}

bool Library::ConsumeXUnionDeclaration(std::unique_ptr<raw::XUnionDeclaration> xunion_declaration) {
    auto name = Name(this, xunion_declaration->identifier->location());

    std::vector<XUnion::Member> members;
    for (auto& member : xunion_declaration->members) {
        std::unique_ptr<raw::Ordinal> ordinal =
            std::make_unique<raw::Ordinal>(fidl::ordinals::GetOrdinal(library_name_, name.name_part(), *member));

        auto location = member->identifier->location();
        std::unique_ptr<TypeConstructor> type_ctor;
        if (!ConsumeTypeConstructor(std::move(member->type_ctor), location, &type_ctor))
            return false;

        if (type_ctor->nullability != types::Nullability::kNonnullable) {
            return Fail(member->location(), "Extensible union members cannot be nullable");
        }

        members.emplace_back(std::move(ordinal), std::move(type_ctor), location, std::move(member->attributes));
    }

    xunion_declarations_.push_back(
        std::make_unique<XUnion>(std::move(xunion_declaration->attributes), std::move(name), std::move(members)));
    return RegisterDecl(xunion_declarations_.back().get());
}

bool Library::ConsumeFile(std::unique_ptr<raw::File> file) {
    if (file->attributes) {
        ValidateAttributesPlacement(AttributeSchema::Placement::kLibrary, file->attributes.get());
        if (!attributes_) {
            attributes_ = std::move(file->attributes);
        } else {
            AttributesBuilder attributes_builder(error_reporter_, std::move(attributes_->attributes));
            for (auto& attribute : file->attributes->attributes) {
                if (!attributes_builder.Insert(std::move(attribute)))
                    return false;
            }
            attributes_ = std::make_unique<raw::AttributeList>(raw::SourceElement(file->attributes->start_, file->attributes->end_),
                                                               attributes_builder.Done());
        }
    }

    // All fidl files in a library should agree on the library name.
    std::vector<StringView> new_name;
    for (const auto& part : file->library_name->components) {
        new_name.push_back(part->location().data());
    }
    if (!library_name_.empty()) {
        if (new_name != library_name_) {
            return Fail(file->library_name->components[0]->location(),
                        "Two files in the library disagree about the name of the library");
        }
    } else {
        library_name_ = new_name;
    }

    auto using_list = std::move(file->using_list);
    for (auto& using_directive : using_list) {
        if (!ConsumeUsing(std::move(using_directive))) {
            return false;
        }
    }

    auto bits_declaration_list = std::move(file->bits_declaration_list);
    for (auto& bits_declaration : bits_declaration_list) {
        if (!ConsumeBitsDeclaration(std::move(bits_declaration))) {
            return false;
        }
    }

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

    auto table_declaration_list = std::move(file->table_declaration_list);
    for (auto& table_declaration : table_declaration_list) {
        if (!ConsumeTableDeclaration(std::move(table_declaration))) {
            return false;
        }
    }

    auto union_declaration_list = std::move(file->union_declaration_list);
    for (auto& union_declaration : union_declaration_list) {
        if (!ConsumeUnionDeclaration(std::move(union_declaration))) {
            return false;
        }
    }

    auto xunion_declaration_list = std::move(file->xunion_declaration_list);
    for (auto& xunion_declaration : xunion_declaration_list) {
        if (!ConsumeXUnionDeclaration(std::move(xunion_declaration))) {
            return false;
        }
    }

    return true;
}

bool Library::ResolveConstant(Constant* constant, const Type* type) {
    assert(constant != nullptr);

    if (constant->IsResolved())
        return true;

    switch (constant->kind) {
    case Constant::Kind::kIdentifier: {
        auto identifier_constant = static_cast<IdentifierConstant*>(constant);
        return ResolveIdentifierConstant(identifier_constant, type);
    }
    case Constant::Kind::kLiteral: {
        auto literal_constant = static_cast<LiteralConstant*>(constant);
        return ResolveLiteralConstant(literal_constant, type);
    }
    case Constant::Kind::kSynthesized: {
        assert(false && "Compiler bug: synthesized constant does not have a resolved value!");
    }
    }

    __UNREACHABLE;
}

bool Library::ResolveIdentifierConstant(IdentifierConstant* identifier_constant, const Type* type) {
    assert(TypeCanBeConst(type) &&
           "Compiler bug: resolving identifier constant to non-const-able type!");

    auto decl = LookupDeclByName(identifier_constant->name);
    if (!decl || decl->kind != Decl::Kind::kConst)
        return false;

    // Recursively resolve constants
    auto const_decl = static_cast<Const*>(decl);
    if (!CompileConst(const_decl))
        return false;
    assert(const_decl->value->IsResolved());

    const ConstantValue& const_val = const_decl->value->Value();
    std::unique_ptr<ConstantValue> resolved_val;
    switch (type->kind) {
    case Type::Kind::kString: {
        if (!TypeIsConvertibleTo(const_decl->type_ctor->type, type))
            goto fail_cannot_convert;

        if (!const_val.Convert(ConstantValue::Kind::kString, &resolved_val))
            goto fail_cannot_convert;
        break;
    }
    case Type::Kind::kPrimitive: {
        auto primitive_type = static_cast<const PrimitiveType*>(type);
        switch (primitive_type->subtype) {
        case types::PrimitiveSubtype::kBool:
            if (!const_val.Convert(ConstantValue::Kind::kBool, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kInt8:
            if (!const_val.Convert(ConstantValue::Kind::kInt8, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kInt16:
            if (!const_val.Convert(ConstantValue::Kind::kInt16, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kInt32:
            if (!const_val.Convert(ConstantValue::Kind::kInt32, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kInt64:
            if (!const_val.Convert(ConstantValue::Kind::kInt64, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kUint8:
            if (!const_val.Convert(ConstantValue::Kind::kUint8, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kUint16:
            if (!const_val.Convert(ConstantValue::Kind::kUint16, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kUint32:
            if (!const_val.Convert(ConstantValue::Kind::kUint32, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kUint64:
            if (!const_val.Convert(ConstantValue::Kind::kUint64, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kFloat32:
            if (!const_val.Convert(ConstantValue::Kind::kFloat32, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kFloat64:
            if (!const_val.Convert(ConstantValue::Kind::kFloat64, &resolved_val))
                goto fail_cannot_convert;
            break;
        }
        break;
    }
    default: {
        assert(false &&
               "Compiler bug: const-able type not handled during identifier constant resolution!");
    }
    }

    identifier_constant->ResolveTo(std::move(resolved_val));
    return true;

fail_cannot_convert:
    std::ostringstream msg_stream;
    msg_stream << NameFlatConstant(identifier_constant) << ", of type ";
    msg_stream << NameFlatTypeConstructor(const_decl->type_ctor.get());
    msg_stream << ", cannot be converted to type " << NameFlatType(type);
    return Fail(msg_stream.str());
}

bool Library::ResolveLiteralConstant(LiteralConstant* literal_constant, const Type* type) {
    switch (literal_constant->literal->kind) {
    case raw::Literal::Kind::kString: {
        if (type->kind != Type::Kind::kString)
            goto return_fail;
        auto string_type = static_cast<const StringType*>(type);
        auto string_literal = static_cast<raw::StringLiteral*>(literal_constant->literal.get());
        auto string_data = string_literal->location().data();

        // TODO(pascallouis): because data() contains the raw content,
        // with the two " to identify strings, we need to take this
        // into account. We should expose the actual size of string
        // literals properly, and take into account escaping.
        uint64_t string_size = string_data.size() - 2;
        if (string_type->max_size->value < string_size) {
            std::ostringstream msg_stream;
            msg_stream << NameFlatConstant(literal_constant) << " (string:" << string_size;
            msg_stream << ") exceeds the size bound of type " << NameFlatType(type);
            return Fail(literal_constant->literal->location(), msg_stream.str());
        }

        literal_constant->ResolveTo(
            std::make_unique<StringConstantValue>(string_literal->location().data()));
        return true;
    }
    case raw::Literal::Kind::kTrue: {
        if (type->kind != Type::Kind::kPrimitive)
            goto return_fail;
        if (static_cast<const PrimitiveType*>(type)->subtype != types::PrimitiveSubtype::kBool)
            goto return_fail;
        literal_constant->ResolveTo(std::make_unique<BoolConstantValue>(true));
        return true;
    }
    case raw::Literal::Kind::kFalse: {
        if (type->kind != Type::Kind::kPrimitive)
            goto return_fail;
        if (static_cast<const PrimitiveType*>(type)->subtype != types::PrimitiveSubtype::kBool)
            goto return_fail;
        literal_constant->ResolveTo(std::make_unique<BoolConstantValue>(false));
        return true;
    }
    case raw::Literal::Kind::kNumeric: {
        if (type->kind != Type::Kind::kPrimitive)
            goto return_fail;

        // These must be initialized out of line to allow for goto statement
        const raw::NumericLiteral* numeric_literal;
        const PrimitiveType* primitive_type;
        numeric_literal =
            static_cast<const raw::NumericLiteral*>(literal_constant->literal.get());
        primitive_type = static_cast<const PrimitiveType*>(type);
        switch (primitive_type->subtype) {
        case types::PrimitiveSubtype::kInt8: {
            int8_t value;
            if (!ParseNumericLiteral<int8_t>(numeric_literal, &value))
                goto return_fail;
            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<int8_t>>(value));
            return true;
        }
        case types::PrimitiveSubtype::kInt16: {
            int16_t value;
            if (!ParseNumericLiteral<int16_t>(numeric_literal, &value))
                goto return_fail;

            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<int16_t>>(value));
            return true;
        }
        case types::PrimitiveSubtype::kInt32: {
            int32_t value;
            if (!ParseNumericLiteral<int32_t>(numeric_literal, &value))
                goto return_fail;

            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<int32_t>>(value));
            return true;
        }
        case types::PrimitiveSubtype::kInt64: {
            int64_t value;
            if (!ParseNumericLiteral<int64_t>(numeric_literal, &value))
                goto return_fail;

            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<int64_t>>(value));
            return true;
        }
        case types::PrimitiveSubtype::kUint8: {
            uint8_t value;
            if (!ParseNumericLiteral<uint8_t>(numeric_literal, &value))
                goto return_fail;

            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<uint8_t>>(value));
            return true;
        }
        case types::PrimitiveSubtype::kUint16: {
            uint16_t value;
            if (!ParseNumericLiteral<uint16_t>(numeric_literal, &value))
                goto return_fail;

            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<uint16_t>>(value));
            return true;
        }
        case types::PrimitiveSubtype::kUint32: {
            uint32_t value;
            if (!ParseNumericLiteral<uint32_t>(numeric_literal, &value))
                goto return_fail;

            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<uint32_t>>(value));
            return true;
        }
        case types::PrimitiveSubtype::kUint64: {
            uint64_t value;
            if (!ParseNumericLiteral<uint64_t>(numeric_literal, &value))
                goto return_fail;

            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<uint64_t>>(value));
            return true;
        }
        case types::PrimitiveSubtype::kFloat32: {
            float value;
            if (!ParseNumericLiteral<float>(numeric_literal, &value))
                goto return_fail;
            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<float>>(value));
            return true;
        }
        case types::PrimitiveSubtype::kFloat64: {
            double value;
            if (!ParseNumericLiteral<double>(numeric_literal, &value))
                goto return_fail;

            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<double>>(value));
            return true;
        }
        default:
            goto return_fail;
        }

    return_fail:
        std::ostringstream msg_stream;
        msg_stream << NameFlatConstant(literal_constant) << " cannot be interpreted as type ";
        msg_stream << NameFlatType(type);
        return Fail(literal_constant->literal->location(), msg_stream.str());
    }
    }
}

bool Library::TypeCanBeConst(const Type* type) {
    switch (type->kind) {
    case flat::Type::Kind::kString:
        return type->nullability != types::Nullability::kNullable;
    case flat::Type::Kind::kPrimitive:
        return true;
    default:
        return false;
    } // switch
}

bool Library::TypeIsConvertibleTo(const Type* from_type, const Type* to_type) {
    switch (to_type->kind) {
    case flat::Type::Kind::kString: {
        if (from_type->kind != flat::Type::Kind::kString)
            return false;

        auto from_string_type = static_cast<const flat::StringType*>(from_type);
        auto to_string_type = static_cast<const flat::StringType*>(to_type);

        if (to_string_type->nullability == types::Nullability::kNonnullable &&
            from_string_type->nullability != types::Nullability::kNonnullable)
            return false;

        if (to_string_type->max_size->value < from_string_type->max_size->value)
            return false;

        return true;
    }
    case flat::Type::Kind::kPrimitive: {
        if (from_type->kind != flat::Type::Kind::kPrimitive) {
            return false;
        }

        auto from_primitive_type = static_cast<const flat::PrimitiveType*>(from_type);
        auto to_primitive_type = static_cast<const flat::PrimitiveType*>(to_type);

        switch (to_primitive_type->subtype) {
        case types::PrimitiveSubtype::kBool:
            return from_primitive_type->subtype == types::PrimitiveSubtype::kBool;
        default:
            // TODO(pascallouis): be more precise about convertibility, e.g. it
            // should not be allowed to convert a float to an int.
            return from_primitive_type->subtype != types::PrimitiveSubtype::kBool;
        }
    }
    default:
        return false;
    } // switch
}

Decl* Library::LookupConstant(const TypeConstructor* type_ctor, const Name& name) {
    auto decl = LookupDeclByName(type_ctor->name);
    if (decl == nullptr) {
        // This wasn't a named type. Thus we are looking up a
        // top-level constant, of string or primitive type.
        auto iter = constants_.find(&name);
        if (iter == constants_.end()) {
            return nullptr;
        }
        return iter->second;
    }
    // We must otherwise be looking for an enum member.
    if (decl->kind != Decl::Kind::kEnum) {
        return nullptr;
    }
    auto enum_decl = static_cast<Enum*>(decl);
    for (auto& member : enum_decl->members) {
        if (member.name.data() == name.name_part()) {
            return enum_decl;
        }
    }
    // The enum didn't have a member of that name!
    return nullptr;
}

// Library resolution is concerned with resolving identifiers to their
// declarations, and with computing type sizes and alignments.

Decl* Library::LookupDeclByName(const Name& name) const {
    auto iter = declarations_.find(&name);
    if (iter == declarations_.end()) {
        return nullptr;
    }
    return iter->second;
}

template <typename NumericType>
bool Library::ParseNumericLiteral(const raw::NumericLiteral* literal,
                                  NumericType* out_value) const {
    assert(literal != nullptr);
    assert(out_value != nullptr);

    auto data = literal->location().data();
    std::string string_data(data.data(), data.data() + data.size());
    auto result = utils::ParseNumeric(string_data, out_value);
    return result == utils::ParseNumericResult::kSuccess;
}

// Calculating declaration dependencies is largely serving the C/C++ family of languages bindings.
// For instance, the declaration of a struct member type must be defined before the containing
// struct if that member is stored inline.
// Given the FIDL declarations:
//
//     struct D2 { D1 d; }
//     struct D1 { int32 x; }
//
// We must first declare D1, followed by D2 when emitting C code.
//
// Below, an edge from D1 to D2 means that we must see the declaration of of D1 before
// the declaration of D2, i.e. the calculated set of |out_edges| represents all the declarations
// that |decl| depends on.
//
// Notes:
// - Nullable structs do not require dependency edges since they are boxed via a
// pointer indirection, and their content placed out-of-line.
// - However, xunions always require dependency edges since nullability does not affect
// their layout.
bool Library::DeclDependencies(Decl* decl, std::set<Decl*>* out_edges) {
    std::set<Decl*> edges;
    auto maybe_add_decl = [this, &edges](const TypeConstructor* type_ctor) {
        for (;;) {
            const auto& name = type_ctor->name;
            if (name.name_part() == "request") {
                return;
            } else if (type_ctor->maybe_arg_type_ctor) {
                type_ctor = type_ctor->maybe_arg_type_ctor.get();
            } else if (type_ctor->nullability == types::Nullability::kNullable) {
                if (auto decl = LookupDeclByName(name); decl && decl->kind == Decl::Kind::kXUnion) {
                    edges.insert(decl);
                }
                return;
            } else {
                if (auto decl = LookupDeclByName(name); decl && decl->kind != Decl::Kind::kInterface) {
                    edges.insert(decl);
                }
                return;
            }
        }
    };
    auto maybe_add_constant = [this, &edges](const TypeConstructor* type_ctor,
                                             const Constant* constant) -> bool {
        switch (constant->kind) {
        case Constant::Kind::kIdentifier: {
            auto identifier = static_cast<const flat::IdentifierConstant*>(constant);
            auto decl = LookupConstant(type_ctor, identifier->name);
            if (decl == nullptr) {
                std::string message("Unable to find the constant named: ");
                message += identifier->name.name_part();
                return Fail(identifier->name, message.data());
            }
            edges.insert(decl);
            break;
        }
        case Constant::Kind::kLiteral:
        case Constant::Kind::kSynthesized: {
            // Literal and synthesized constants have no dependencies on other declarations.
            break;
        }
        }
        return true;
    };
    switch (decl->kind) {
    case Decl::Kind::kBits: {
        auto bits_decl = static_cast<const Bits*>(decl);
        for (const auto& member : bits_decl->members) {
            maybe_add_constant(bits_decl->subtype_ctor.get(), member.value.get());
        }
        break;
    }
    case Decl::Kind::kConst: {
        auto const_decl = static_cast<const Const*>(decl);
        if (!maybe_add_constant(const_decl->type_ctor.get(), const_decl->value.get()))
            return false;
        break;
    }
    case Decl::Kind::kEnum: {
        auto enum_decl = static_cast<const Enum*>(decl);
        for (const auto& member : enum_decl->members) {
            maybe_add_constant(enum_decl->subtype_ctor.get(), member.value.get());
        }
        break;
    }
    case Decl::Kind::kInterface: {
        auto interface_decl = static_cast<const Interface*>(decl);
        for (const auto& superinterface : interface_decl->superinterfaces) {
            if (auto type_decl = LookupDeclByName(superinterface); type_decl)
                edges.insert(type_decl);
        }
        for (const auto& method : interface_decl->methods) {
            if (method.maybe_request != nullptr) {
                edges.insert(method.maybe_request);
            }
            if (method.maybe_response != nullptr) {
                edges.insert(method.maybe_response);
            }
        }
        break;
    }
    case Decl::Kind::kStruct: {
        auto struct_decl = static_cast<const Struct*>(decl);
        for (const auto& member : struct_decl->members) {
            maybe_add_decl(member.type_ctor.get());
            if (member.maybe_default_value) {
                if (!maybe_add_constant(member.type_ctor.get(), member.maybe_default_value.get()))
                    return false;
            }
        }
        break;
    }
    case Decl::Kind::kTable: {
        auto table_decl = static_cast<const Table*>(decl);
        for (const auto& member : table_decl->members) {
            if (!member.maybe_used)
                continue;
            maybe_add_decl(member.maybe_used->type_ctor.get());
            if (member.maybe_used->maybe_default_value) {
                if (!maybe_add_constant(member.maybe_used->type_ctor.get(),
                                        member.maybe_used->maybe_default_value.get()))
                    return false;
            }
        }
        break;
    }
    case Decl::Kind::kUnion: {
        auto union_decl = static_cast<const Union*>(decl);
        for (const auto& member : union_decl->members) {
            maybe_add_decl(member.type_ctor.get());
        }
        break;
    }
    case Decl::Kind::kXUnion: {
        auto xunion_decl = static_cast<const XUnion*>(decl);
        for (const auto& member : xunion_decl->members) {
            maybe_add_decl(member.type_ctor.get());
        }
        break;
    }
    }
    *out_edges = std::move(edges);
    return true;
}

namespace {
// To compare two Decl's in the same library, it suffices to compare the unqualified names of the Decl's.
struct CmpDeclInLibrary {
    bool operator()(const Decl* a, const Decl* b) const {
        assert(a->name != b->name || a == b);
        return a->name < b->name;
    }
};
} // namespace

bool Library::SortDeclarations() {
    // |degree| is the number of undeclared dependencies for each decl.
    std::map<Decl*, uint32_t, CmpDeclInLibrary> degrees;
    // |inverse_dependencies| records the decls that depend on each decl.
    std::map<Decl*, std::vector<Decl*>, CmpDeclInLibrary> inverse_dependencies;
    for (auto& name_and_decl : declarations_) {
        Decl* decl = name_and_decl.second;
        degrees[decl] = 0u;
    }
    for (auto& name_and_decl : declarations_) {
        Decl* decl = name_and_decl.second;
        std::set<Decl*> deps;
        if (!DeclDependencies(decl, &deps))
            return false;
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
        return Fail("There is an includes-cycle in declarations");
    }

    return true;
}

bool Library::CompileDecl(Decl* decl) {
    Compiling guard(decl);
    switch (decl->kind) {
    case Decl::Kind::kBits: {
        auto bits_decl = static_cast<Bits*>(decl);
        if (!CompileBits(bits_decl))
            return false;
        break;
    }
    case Decl::Kind::kConst: {
        auto const_decl = static_cast<Const*>(decl);
        if (!CompileConst(const_decl))
            return false;
        break;
    }
    case Decl::Kind::kEnum: {
        auto enum_decl = static_cast<Enum*>(decl);
        if (!CompileEnum(enum_decl))
            return false;
        break;
    }
    case Decl::Kind::kInterface: {
        auto interface_decl = static_cast<Interface*>(decl);
        if (!CompileInterface(interface_decl))
            return false;
        break;
    }
    case Decl::Kind::kStruct: {
        auto struct_decl = static_cast<Struct*>(decl);
        if (!CompileStruct(struct_decl))
            return false;
        break;
    }
    case Decl::Kind::kTable: {
        auto table_decl = static_cast<Table*>(decl);
        if (!CompileTable(table_decl))
            return false;
        break;
    }
    case Decl::Kind::kUnion: {
        auto union_decl = static_cast<Union*>(decl);
        if (!CompileUnion(union_decl))
            return false;
        break;
    }
    case Decl::Kind::kXUnion: {
        auto xunion_decl = static_cast<XUnion*>(decl);
        if (!CompileXUnion(xunion_decl))
            return false;
        break;
    }
    } // switch
    return true;
}

bool Library::VerifyDeclAttributes(Decl* decl) {
    assert(decl->compiled && "verification must happen after compilation of decls");
    auto placement_ok = error_reporter_->Checkpoint();
    switch (decl->kind) {
    case Decl::Kind::kBits: {
        auto bits_declaration = static_cast<Bits*>(decl);
        // Attributes: check placement.
        ValidateAttributesPlacement(
            AttributeSchema::Placement::kBitsDecl,
            bits_declaration->attributes.get());
        for (const auto& member : bits_declaration->members) {
            ValidateAttributesPlacement(
                AttributeSchema::Placement::kBitsMember,
                member.attributes.get());
        }
        if (placement_ok.NoNewErrors()) {
            // Attributes: check constraints.
            ValidateAttributesConstraints(
                bits_declaration,
                bits_declaration->attributes.get());
        }
        break;
    }
    case Decl::Kind::kConst: {
        auto const_decl = static_cast<Const*>(decl);
        // Attributes: for const declarations, we only check placement.
        ValidateAttributesPlacement(
            AttributeSchema::Placement::kConstDecl, const_decl->attributes.get());
        break;
    }
    case Decl::Kind::kEnum: {
        auto enum_declaration = static_cast<Enum*>(decl);
        // Attributes: check placement.
        ValidateAttributesPlacement(
            AttributeSchema::Placement::kEnumDecl,
            enum_declaration->attributes.get());
        for (const auto& member : enum_declaration->members) {
            ValidateAttributesPlacement(
                AttributeSchema::Placement::kEnumMember,
                member.attributes.get());
        }
        if (placement_ok.NoNewErrors()) {
            // Attributes: check constraints.
            ValidateAttributesConstraints(
                enum_declaration,
                enum_declaration->attributes.get());
        }
        break;
    }
    case Decl::Kind::kInterface: {
        auto interface_declaration = static_cast<Interface*>(decl);
        // Attributes: check placement.
        ValidateAttributesPlacement(
            AttributeSchema::Placement::kInterfaceDecl,
            interface_declaration->attributes.get());
        for (const auto method : interface_declaration->all_methods) {
            ValidateAttributesPlacement(
                AttributeSchema::Placement::kMethod,
                method->attributes.get());
        }
        if (placement_ok.NoNewErrors()) {
            // Attributes: check constraints.
            for (const auto method : interface_declaration->all_methods) {
                if (method->maybe_request) {
                    ValidateAttributesConstraints(
                        method->maybe_request,
                        interface_declaration->attributes.get());
                    ValidateAttributesConstraints(
                        method->maybe_request,
                        method->attributes.get());
                }
                if (method->maybe_response) {
                    ValidateAttributesConstraints(
                        method->maybe_response,
                        interface_declaration->attributes.get());
                    ValidateAttributesConstraints(
                        method->maybe_response,
                        method->attributes.get());
                }
            }
        }
        break;
    }
    case Decl::Kind::kStruct: {
        auto struct_declaration = static_cast<Struct*>(decl);
        // Attributes: check placement.
        ValidateAttributesPlacement(
            AttributeSchema::Placement::kStructDecl,
            struct_declaration->attributes.get());
        for (const auto& member : struct_declaration->members) {
            ValidateAttributesPlacement(
                AttributeSchema::Placement::kStructMember,
                member.attributes.get());
        }
        if (placement_ok.NoNewErrors()) {
            // Attributes: check constraint.
            ValidateAttributesConstraints(
                struct_declaration,
                struct_declaration->attributes.get());
        }
        break;
    }
    case Decl::Kind::kTable: {
        auto table_declaration = static_cast<Table*>(decl);
        // Attributes: check placement.
        ValidateAttributesPlacement(
            AttributeSchema::Placement::kTableDecl,
            table_declaration->attributes.get());
        for (const auto& member : table_declaration->members) {
            if (member.maybe_used) {
                ValidateAttributesPlacement(
                    AttributeSchema::Placement::kTableMember,
                    member.maybe_used->attributes.get());
            }
        }
        if (placement_ok.NoNewErrors()) {
            // Attributes: check constraint.
            ValidateAttributesConstraints(
                table_declaration,
                table_declaration->attributes.get());
        }
        break;
    }
    case Decl::Kind::kUnion: {
        auto union_declaration = static_cast<Union*>(decl);
        // Attributes: check placement.
        ValidateAttributesPlacement(
            AttributeSchema::Placement::kUnionDecl,
            union_declaration->attributes.get());
        for (const auto& member : union_declaration->members) {
            ValidateAttributesPlacement(
                AttributeSchema::Placement::kUnionMember,
                member.attributes.get());
        }
        if (placement_ok.NoNewErrors()) {
            // Attributes: check constraint.
            ValidateAttributesConstraints(
                union_declaration,
                union_declaration->attributes.get());
        }
        break;
    }
    case Decl::Kind::kXUnion: {
        auto xunion_declaration = static_cast<XUnion*>(decl);
        // Attributes: check placement.
        ValidateAttributesPlacement(
            AttributeSchema::Placement::kXUnionDecl,
            xunion_declaration->attributes.get());
        for (const auto& member : xunion_declaration->members) {
            ValidateAttributesPlacement(
                AttributeSchema::Placement::kXUnionMember,
                member.attributes.get());
        }
        if (placement_ok.NoNewErrors()) {
            // Attributes: check constraint.
            ValidateAttributesConstraints(
                xunion_declaration,
                xunion_declaration->attributes.get());
        }
    }
    } // switch
    return true;
}

bool Library::CompileBits(Bits* bits_declaration) {
    if (!CompileTypeConstructor(bits_declaration->subtype_ctor.get(), &bits_declaration->typeshape))
        return false;

    if (bits_declaration->subtype_ctor->type->kind != Type::Kind::kPrimitive) {
        std::string message("bits may only be of unsigned integral primitive type, found ");
        message.append(NameFlatType(bits_declaration->subtype_ctor->type));
        return Fail(*bits_declaration, message);
    }

    // Validate constants.
    auto primitive_type = static_cast<const PrimitiveType*>(bits_declaration->subtype_ctor->type);
    switch (primitive_type->subtype) {
    case types::PrimitiveSubtype::kUint8:
        if (!ValidateBitsMembers<uint8_t>(bits_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kUint16:
        if (!ValidateBitsMembers<uint16_t>(bits_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kUint32:
        if (!ValidateBitsMembers<uint32_t>(bits_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kUint64:
        if (!ValidateBitsMembers<uint64_t>(bits_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kBool:
    case types::PrimitiveSubtype::kInt8:
    case types::PrimitiveSubtype::kInt16:
    case types::PrimitiveSubtype::kInt32:
    case types::PrimitiveSubtype::kInt64:
    case types::PrimitiveSubtype::kFloat32:
    case types::PrimitiveSubtype::kFloat64:
        std::string message("bits may only be of unsigned integral primitive type, found ");
        message.append(NameFlatType(bits_declaration->subtype_ctor->type));
        return Fail(*bits_declaration, message);
    }

    return true;
}

bool Library::CompileConst(Const* const_declaration) {
    TypeShape typeshape;
    if (!CompileTypeConstructor(const_declaration->type_ctor.get(), &typeshape))
        return false;
    const auto* const_type = const_declaration->type_ctor.get()->type;
    if (!TypeCanBeConst(const_type)) {
        std::ostringstream msg_stream;
        msg_stream << "invalid constant type " << NameFlatType(const_type);
        return Fail(*const_declaration, msg_stream.str());
    }
    if (!ResolveConstant(const_declaration->value.get(), const_type))
        return Fail(*const_declaration, "unable to resolve constant value");

    return true;
}

bool Library::CompileEnum(Enum* enum_declaration) {
    if (!CompileTypeConstructor(enum_declaration->subtype_ctor.get(), &enum_declaration->typeshape))
        return false;

    if (enum_declaration->subtype_ctor->type->kind != Type::Kind::kPrimitive) {
        std::string message("enums may only be of integral primitive type, found ");
        message.append(NameFlatType(enum_declaration->subtype_ctor->type));
        return Fail(*enum_declaration, message);
    }

    // Validate constants.
    auto primitive_type = static_cast<const PrimitiveType*>(enum_declaration->subtype_ctor->type);
    enum_declaration->type = primitive_type;
    switch (primitive_type->subtype) {
    case types::PrimitiveSubtype::kInt8:
        if (!ValidateEnumMembers<int8_t>(enum_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kInt16:
        if (!ValidateEnumMembers<int16_t>(enum_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kInt32:
        if (!ValidateEnumMembers<int32_t>(enum_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kInt64:
        if (!ValidateEnumMembers<int64_t>(enum_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kUint8:
        if (!ValidateEnumMembers<uint8_t>(enum_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kUint16:
        if (!ValidateEnumMembers<uint16_t>(enum_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kUint32:
        if (!ValidateEnumMembers<uint32_t>(enum_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kUint64:
        if (!ValidateEnumMembers<uint64_t>(enum_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kBool:
    case types::PrimitiveSubtype::kFloat32:
    case types::PrimitiveSubtype::kFloat64:
        std::string message("enums may only be of integral primitive type, found ");
        message.append(NameFlatType(enum_declaration->subtype_ctor->type));
        return Fail(*enum_declaration, message);
    }

    return true;
}

bool HasSimpleLayout(const Decl* decl) {
    return decl->GetAttribute("Layout") == "Simple";
}

bool Library::CompileInterface(Interface* interface_declaration) {
    MethodScope method_scope;
    auto CheckScopes = [this, &interface_declaration, &method_scope](const Interface* interface, auto Visitor) -> bool {
        for (const auto& name : interface->superinterfaces) {
            auto decl = LookupDeclByName(name);
            // TODO(FIDL-603): Special handling here should not be required, we
            // should first rely on creating the types representing composed
            // protocols.
            if (!decl) {
                std::string message("unknown type ");
                message.append(name.name_part());
                return Fail(name, message);
            }
            if (decl->kind != Decl::Kind::kInterface)
                return Fail(name, "This superinterface declaration is not an interface");
            if (!decl->HasAttribute("FragileBase")) {
                std::string message = "interface ";
                message += NameName(name, ".", "/");
                message += " is not marked by [FragileBase] attribute, disallowing interface ";
                message += NameName(interface_declaration->name, ".", "/");
                message += " from inheriting from it";
                return Fail(name, message);
            }
            auto superinterface = static_cast<const Interface*>(decl);
            auto maybe_location = superinterface->name.maybe_location();
            assert(maybe_location);
            if (method_scope.interfaces.Insert(superinterface, *maybe_location).ok()) {
                if (!Visitor(superinterface, Visitor))
                    return false;
            } else {
                // Otherwise we have already seen this interface in
                // the inheritance graph.
            }
        }
        for (const auto& method : interface->methods) {
            auto name_result = method_scope.names.Insert(method.name.data(), method.name);
            if (!name_result.ok())
                return Fail(method.name,
                            "Multiple methods with the same name in an interface; last occurrence was at " +
                                name_result.previous_occurrence().position());
            auto ordinal_result = method_scope.ordinals.Insert(method.ordinal->value, method.name);
            if (method.ordinal->value == 0)
                return Fail(method.ordinal->location(), "Ordinal value 0 disallowed.");
            if (!ordinal_result.ok()) {
                std::string replacement_method(
                    fidl::ordinals::GetSelector(method.attributes.get(), method.name));
                replacement_method.push_back('_');
                return Fail(method.ordinal->location(),
                            "Multiple methods with the same ordinal in an interface; previous was at " +
                                ordinal_result.previous_occurrence().position() + ". If these " +
                                "were automatically generated, consider using attribute " +
                                "[Selector=\"" + replacement_method + "\"] to change the " +
                                "name used to calculate the ordinal.");
            }

            // Add a pointer to this method to the interface_declarations list.
            interface_declaration->all_methods.push_back(&method);
        }
        return true;
    };
    if (!CheckScopes(interface_declaration, CheckScopes))
        return false;

    interface_declaration->typeshape = HandleType::Shape();

    for (auto& method : interface_declaration->methods) {
        auto CreateMessage = [&](Struct* message) -> bool {
            Scope<StringView> scope;
            for (auto& param : message->members) {
                if (!scope.Insert(param.name.data(), param.name).ok())
                    return Fail(param.name, "Multiple parameters with the same name in a method");
                if (!CompileTypeConstructor(param.type_ctor.get(), &param.fieldshape.Typeshape()))
                    return false;
            }
            return true;
        };
        if (method.maybe_request) {
            if (!CreateMessage(method.maybe_request))
                return false;
        }
        if (method.maybe_response) {
            if (!CreateMessage(method.maybe_response))
                return false;
        }
    }

    return true;
}

bool Library::CompileStruct(Struct* struct_declaration) {
    Scope<StringView> scope;
    std::vector<FieldShape*> fidl_struct;

    uint32_t max_member_handles = 0;
    for (auto& member : struct_declaration->members) {
        auto name_result = scope.Insert(member.name.data(), member.name);
        if (!name_result.ok())
            return Fail(member.name,
                        "Multiple struct fields with the same name; previous was at " +
                            name_result.previous_occurrence().position());
        if (!CompileTypeConstructor(member.type_ctor.get(), &member.fieldshape.Typeshape()))
            return false;
        fidl_struct.push_back(&member.fieldshape);
    }

    if (struct_declaration->recursive) {
        max_member_handles = std::numeric_limits<uint32_t>::max();
    } else {
        // Member handles will be counted by CStructTypeShape.
        max_member_handles = 0;
    }

    struct_declaration->typeshape = Struct::Shape(&fidl_struct, max_member_handles);

    return true;
}

bool Library::CompileTable(Table* table_declaration) {
    Scope<StringView> name_scope;
    Scope<uint32_t> ordinal_scope;

    uint32_t max_member_handles = 0;
    for (auto& member : table_declaration->members) {
        auto ordinal_result = ordinal_scope.Insert(member.ordinal->value, member.ordinal->location());
        if (!ordinal_result.ok())
            return Fail(member.ordinal->location(),
                        "Multiple table fields with the same ordinal; previous was at " +
                            ordinal_result.previous_occurrence().position());
        if (member.maybe_used) {
            auto name_result = name_scope.Insert(member.maybe_used->name.data(), member.maybe_used->name);
            if (!name_result.ok())
                return Fail(member.maybe_used->name,
                            "Multiple table fields with the same name; previous was at " +
                                name_result.previous_occurrence().position());
            if (!CompileTypeConstructor(member.maybe_used->type_ctor.get(), &member.maybe_used->typeshape))
                return false;
        }
    }

    uint32_t last_ordinal_seen = 0;
    for (const auto& ordinal_and_loc : ordinal_scope) {
        if (ordinal_and_loc.first != last_ordinal_seen + 1) {
            return Fail(ordinal_and_loc.second,
                        "Missing ordinal (table ordinals do not form a dense space)");
        }
        last_ordinal_seen = ordinal_and_loc.first;
    }

    if (table_declaration->recursive) {
        max_member_handles = std::numeric_limits<uint32_t>::max();
    } else {
        // Member handles will be counted by CTableTypeShape.
        max_member_handles = 0;
    }

    std::vector<TypeShape*> fields(table_declaration->members.size());
    for (auto& member : table_declaration->members) {
        if (member.maybe_used) {
            fields[member.ordinal->value - 1] = &member.maybe_used->typeshape;
        }
    }

    table_declaration->typeshape = Table::Shape(&fields, max_member_handles);

    return true;
}

bool Library::CompileUnion(Union* union_declaration) {
    Scope<StringView> scope;
    for (auto& member : union_declaration->members) {
        auto name_result = scope.Insert(member.name.data(), member.name);
        if (!name_result.ok())
            return Fail(member.name,
                        "Multiple union members with the same name; previous was at " +
                            name_result.previous_occurrence().position());
        if (!CompileTypeConstructor(member.type_ctor.get(), &member.fieldshape.Typeshape()))
            return false;
    }

    auto tag = FieldShape(PrimitiveType::Shape(types::PrimitiveSubtype::kUint32));
    union_declaration->membershape = FieldShape(Union::Shape(union_declaration->members));
    uint32_t extra_handles = 0;
    if (union_declaration->recursive && union_declaration->membershape.MaxHandles()) {
        extra_handles = std::numeric_limits<uint32_t>::max();
    }
    std::vector<FieldShape*> fidl_union = {&tag, &union_declaration->membershape};
    union_declaration->typeshape = Struct::Shape(&fidl_union, extra_handles);

    // This is either 4 or 8, depending on whether any union members
    // have alignment 8.
    auto offset = union_declaration->membershape.Offset();
    for (auto& member : union_declaration->members) {
        member.fieldshape.SetOffset(offset);
    }

    return true;
}

bool Library::CompileXUnion(XUnion* xunion_declaration) {
    Scope<StringView> scope;
    Scope<uint32_t> ordinal_scope;

    for (auto& member : xunion_declaration->members) {
        auto ordinal_result = ordinal_scope.Insert(member.ordinal->value, member.ordinal->location());
        if (!ordinal_result.ok())
            return Fail(member.ordinal->location(),
                        "Multiple xunion fields with the same ordinal; previous was at " +
                            ordinal_result.previous_occurrence().position());

        auto name_result = scope.Insert(member.name.data(), member.name);
        if (!name_result.ok())
            return Fail(member.name,
                        "Multiple xunion members with the same name; previous was at " +
                            name_result.previous_occurrence().position());

        if (!CompileTypeConstructor(member.type_ctor.get(), &member.fieldshape.Typeshape()))
            return false;
    }

    uint32_t max_member_handles;
    if (xunion_declaration->recursive) {
        max_member_handles = std::numeric_limits<uint32_t>::max();
    } else {
        // Member handles will be counted by CXUnionTypeShape.
        max_member_handles = 0u;
    }

    xunion_declaration->typeshape = XUnion::Shape(xunion_declaration->members, max_member_handles);

    return true;
}

bool Library::CompileLibraryName() {
    const std::regex pattern("^[a-z][a-z0-9]*$");
    for (const auto& part_view : library_name_) {
        std::string part = part_view;
        if (!std::regex_match(part, pattern)) {
            return Fail("Invalid library name part " + part);
        }
    }
    return true;
}

bool Library::Compile() {
    for (const auto& dep_library : dependencies_.dependencies()) {
        constants_.insert(dep_library->constants_.begin(), dep_library->constants_.end());
    }

    // Verify that the library's name is valid.
    if (!CompileLibraryName()) {
        return false;
    }

    if (!SortDeclarations()) {
        return false;
    }

    // We process declarations in topologically sorted order. For
    // example, we process a struct member's type before the entire
    // struct.
    for (Decl* decl : declaration_order_) {
        if (!CompileDecl(decl))
            return false;
    }

    // Beware, hacky solution: method request and response are structs, whose
    // typeshape is computed separately. However, in the JSON IR, we add 16 bytes
    // to request and response to account for the header size (and ensure the
    // alignment is at least 4 bytes). Now that we represent request and responses
    // as structs, we should represent this differently in the JSON IR, and let the
    // backends figure this out, or better yet, describe the header directly.
    //
    // For now though, we fixup the representation after the fact.
    for (auto& interface_decl : interface_declarations_) {
        for (auto& method : interface_decl->all_methods) {
            auto FixupMessage = [&](Struct* message) {
                auto header_field_shape = FieldShape(TypeShape(16u, 4u));
                std::vector<FieldShape*> message_struct;
                message_struct.push_back(&header_field_shape);
                for (auto& param : message->members)
                    message_struct.push_back(&param.fieldshape);
                message->typeshape = FidlMessageTypeShape(&message_struct);
            };
            if (method->maybe_request)
                FixupMessage(method->maybe_request);
            if (method->maybe_response)
                FixupMessage(method->maybe_response);
        }
    }

    for (Decl* decl : declaration_order_) {
        if (!VerifyDeclAttributes(decl))
            return false;
    }

    return error_reporter_->errors().size() == 0;
}

bool Library::CompileTypeConstructor(TypeConstructor* type_ctor, TypeShape* out_typeshape) {
    const Type* maybe_arg_type = nullptr;
    if (type_ctor->maybe_arg_type_ctor != nullptr) {
        if (!CompileTypeConstructor(type_ctor->maybe_arg_type_ctor.get(), nullptr))
            return false;
        maybe_arg_type = type_ctor->maybe_arg_type_ctor->type;
    }
    const Size* size = nullptr;
    if (type_ctor->maybe_size != nullptr) {
        if (!ResolveConstant(type_ctor->maybe_size.get(), &kSizeType))
            return Fail(type_ctor->name.maybe_location(), "unable to parse size bound");
        size = static_cast<const Size*>(&type_ctor->maybe_size->Value());
    }
    if (!typespace_->Create(type_ctor->name, maybe_arg_type, type_ctor->handle_subtype,
                            size, type_ctor->nullability,
                            &type_ctor->type))
        return false;
    if (out_typeshape)
        *out_typeshape = type_ctor->type->shape;
    return true;
}

template <typename DeclType, typename MemberType>
bool Library::ValidateMembers(DeclType* decl, MemberValidator<MemberType> validator) {
    assert(decl != nullptr);

    constexpr const char* decl_type = std::is_same_v<DeclType, Enum> ? "enum" : "bits";

    Scope<std::string> name_scope;
    Scope<MemberType> value_scope;
    bool success = true;
    for (auto& member : decl->members) {
        assert(member.value != nullptr && "Compiler bug: member value is null!");

        if (!ResolveConstant(member.value.get(), decl->subtype_ctor->type)) {
            std::string failure_message = "unable to resolve ";
            failure_message += decl_type;
            failure_message += " member";
            return Fail(member.name, failure_message);
        }

        // Check that the member identifier hasn't been used yet
        std::string name = NameIdentifier(member.name);
        auto name_result = name_scope.Insert(name, member.name);
        if (!name_result.ok()) {
            std::ostringstream msg_stream;
            msg_stream << "name of member " << name;
            msg_stream << " conflicts with previously declared member in the ";
            msg_stream << decl_type << " " << decl->GetName();

            // We can log the error and then continue validating for other issues in the decl
            success = Fail(member.name, msg_stream.str());
        }

        MemberType value = static_cast<const NumericConstantValue<MemberType>&>(
                               member.value->Value())
                               .value;
        auto value_result = value_scope.Insert(value, member.name);
        if (!value_result.ok()) {
            std::ostringstream msg_stream;
            msg_stream << "value of member " << name;
            msg_stream << " conflicts with previously declared member ";
            msg_stream << NameIdentifier(value_result.previous_occurrence()) << " in the ";
            msg_stream << decl_type << " " << decl->GetName();

            // We can log the error and then continue validating other members for other bugs
            success = Fail(member.name, msg_stream.str());
        }

        std::string validation_failure;
        if (!validator(value, &validation_failure)) {
            success = Fail(member.name, validation_failure);
        }
    }

    return success;
}

template <typename T>
static bool IsPowerOfTwo(T t) {
    if (t == 0) {
        return false;
    }
    if ((t & (t - 1)) != 0) {
        return false;
    }
    return true;
}

template <typename MemberType>
bool Library::ValidateBitsMembers(Bits* bits_decl) {
    static_assert(std::is_unsigned<MemberType>::value && !std::is_same<MemberType, bool>::value,
                  "Bits members must be an unsigned integral type!");
    // Each bits member must be a power of two.
    auto validator = [](MemberType member, std::string* out_error) {
        if (!IsPowerOfTwo(member)) {
            *out_error = "bits members must be powers of two";
            return false;
        }
        return true;
    };
    return ValidateMembers<Bits, MemberType>(bits_decl, validator);
}

template <typename MemberType>
bool Library::ValidateEnumMembers(Enum* enum_decl) {
    static_assert(std::is_integral<MemberType>::value && !std::is_same<MemberType, bool>::value,
                  "Enum members must be an integral type!");
    // No additional validation is required for enums.
    auto validator = [](MemberType member, std::string* out_error) { return true; };
    return ValidateMembers<Enum, MemberType>(enum_decl, validator);
}

bool Library::HasAttribute(fidl::StringView name) const {
    if (!attributes_)
        return false;
    return attributes_->HasAttribute(name);
}

const std::set<Library*>& Library::dependencies() const {
    return dependencies_.dependencies();
}

} // namespace flat
} // namespace fidl
