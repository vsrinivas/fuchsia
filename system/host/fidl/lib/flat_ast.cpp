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

constexpr uint32_t kMessageAlign = 8u;

class ScopeInsertResult {
public:
    explicit ScopeInsertResult(std::unique_ptr<SourceLocation> previous_occurance)
        : previous_occurance_(std::move(previous_occurance)) {}

    static ScopeInsertResult Ok() { return ScopeInsertResult(nullptr); }
    static ScopeInsertResult FailureAt(SourceLocation previous) {
        return ScopeInsertResult(std::make_unique<SourceLocation>(previous));
    }

    bool ok() const {
        return previous_occurance_ == nullptr;
    }

    const SourceLocation& previous_occurance() const {
        assert(!ok());
        return *previous_occurance_;
    }

private:
    std::unique_ptr<SourceLocation> previous_occurance_;
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

constexpr TypeShape kHandleTypeShape = TypeShape(4u, 4u, 0u, 1u);
constexpr TypeShape kInt8TypeShape = TypeShape(1u, 1u);
constexpr TypeShape kInt16TypeShape = TypeShape(2u, 2u);
constexpr TypeShape kInt32TypeShape = TypeShape(4u, 4u);
constexpr TypeShape kInt64TypeShape = TypeShape(8u, 8u);
constexpr TypeShape kUint8TypeShape = TypeShape(1u, 1u);
constexpr TypeShape kUint16TypeShape = TypeShape(2u, 2u);
constexpr TypeShape kUint32TypeShape = TypeShape(4u, 4u);
constexpr TypeShape kUint64TypeShape = TypeShape(8u, 8u);
constexpr TypeShape kBoolTypeShape = TypeShape(1u, 1u);
constexpr TypeShape kFloat32TypeShape = TypeShape(4u, 4u);
constexpr TypeShape kFloat64TypeShape = TypeShape(8u, 8u);

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

TypeShape CStructTypeShape(std::vector<FieldShape*>* fields, uint32_t extra_handles = 0u) {
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

TypeShape CUnionTypeShape(const std::vector<flat::Union::Member>& members) {
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

TypeShape FidlStructTypeShape(std::vector<FieldShape*>* fields) {
    return CStructTypeShape(fields);
}

TypeShape FidlMessageTypeShape(std::vector<FieldShape*>* fields) {
    auto struct_shape =  FidlStructTypeShape(fields);
    return AlignTypeshape(struct_shape, kMessageAlign);
}

TypeShape PointerTypeShape(TypeShape element, uint32_t max_element_count = 1u) {
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

TypeShape CEnvelopeTypeShape(TypeShape contained_type) {
    auto packed_sizes_field = FieldShape(kUint64TypeShape);
    auto pointer_type = FieldShape(PointerTypeShape(contained_type));
    std::vector<FieldShape*> header{&packed_sizes_field, &pointer_type};
    return CStructTypeShape(&header);
}

TypeShape CTableTypeShape(std::vector<TypeShape*>* fields, uint32_t extra_handles = 0u) {
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
    auto num_fields = FieldShape(kUint32TypeShape);
    auto data_field = FieldShape(PointerTypeShape(pointer_element));
    std::vector<FieldShape*> header{&num_fields, &data_field};
    return CStructTypeShape(&header, extra_handles);
}

TypeShape ArrayTypeShape(TypeShape element, uint32_t count) {
    // TODO(FIDL-345): once TypeShape builders are done and methods can fail, do a
    // __builtin_mul_overflow and fail on overflow instead of ClampedMultiply(element.Size(), count)
    return TypeShape(ClampedMultiply(element.Size(), count),
                     element.Alignment(),
                     element.Depth(),
                     ClampedMultiply(element.MaxHandles(), count),
                     ClampedMultiply(element.MaxOutOfLine(), count));
}

TypeShape VectorTypeShape(TypeShape element, uint32_t max_element_count) {
    auto size = FieldShape(kUint64TypeShape);
    auto data = FieldShape(PointerTypeShape(element, max_element_count));
    std::vector<FieldShape*> header{&size, &data};
    return CStructTypeShape(&header);
}

TypeShape StringTypeShape(uint32_t max_length) {
    auto size = FieldShape(kUint64TypeShape);
    auto data = FieldShape(PointerTypeShape(kUint8TypeShape, max_length));
    std::vector<FieldShape*> header{&size, &data};
    return CStructTypeShape(&header, 0);
}

TypeShape PrimitiveTypeShape(types::PrimitiveSubtype type) {
    switch (type) {
    case types::PrimitiveSubtype::kInt8:
        return kInt8TypeShape;
    case types::PrimitiveSubtype::kInt16:
        return kInt16TypeShape;
    case types::PrimitiveSubtype::kInt32:
        return kInt32TypeShape;
    case types::PrimitiveSubtype::kInt64:
        return kInt64TypeShape;
    case types::PrimitiveSubtype::kUint8:
        return kUint8TypeShape;
    case types::PrimitiveSubtype::kUint16:
        return kUint16TypeShape;
    case types::PrimitiveSubtype::kUint32:
        return kUint32TypeShape;
    case types::PrimitiveSubtype::kUint64:
        return kUint64TypeShape;
    case types::PrimitiveSubtype::kBool:
        return kBoolTypeShape;
    case types::PrimitiveSubtype::kFloat32:
        return kFloat32TypeShape;
    case types::PrimitiveSubtype::kFloat64:
        return kFloat64TypeShape;
    }
}

} // namespace

bool Decl::HasAttribute(fidl::StringView name) const {
    if (!attributes)
        return false;
    return attributes->HasAttribute(name);
}

fidl::StringView Decl::GetAttribute(fidl::StringView name) const {
    if (!attributes)
        return fidl::StringView();
    for (const auto& attribute : attributes->attributes) {
        if (StringView(attribute->name) == name) {
            if (attribute->value != "") {
                const auto& value = attribute->value;
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
        auto element_count = static_cast<const Size&>(vector_type->element_count->Value());
        if (element_count == Size::Max())
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
        auto max_size = static_cast<const Size&>(string_type->max_size->Value());
        return max_size < Size::Max();
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

Type* Typespace::Lookup(const flat::Name& name, types::Nullability nullability,
                        LookupMode lookup_mode) {
    assert(lookup_mode == LookupMode::kNoForwardReferences && "forward refs not implemented yet");

    const auto by_nullability_iter = named_types_.find(nullability);
    if (by_nullability_iter != named_types_.end()) {
        // Direct lookup.
        const ByName& by_name = by_nullability_iter->second;
        auto by_name_iter1 = by_name.find(&name);
        if (by_name_iter1 != by_name.end())
            return by_name_iter1->second.get();

        // Global lookup.
        Name global_name(nullptr, name.name_part());
        auto by_name_iter2 = by_name.find(&global_name);
        if (by_name_iter2 != by_name.end())
            return by_name_iter2->second.get();
    }

    // Unknown.
    switch (lookup_mode) {
    case LookupMode::kNoForwardReferences:
        return nullptr;
    case LookupMode::kAllowForwardReferences:
        return CreateForwardDeclaredType(name, nullability);
    }
}

bool NoOpConstraint(ErrorReporter* error_reporter,
                    const raw::Attribute* attribute,
                    const Decl* decl) {
    return true;
}

AttributeSchema::AttributeSchema(const std::set<Placement>& allowed_placements,
                                 const std::set<std::string> allowed_values,
                                 Constraint constraint)
        : allowed_placements_(allowed_placements),
          allowed_values_(allowed_values),
          constraint_(std::move(constraint)) {}

void AttributeSchema::ValidatePlacement(ErrorReporter* error_reporter,
                                        const raw::Attribute* attribute,
                                        Placement placement) const {
    if (allowed_placements_.size() == 0)
        return;
    auto iter = allowed_placements_.find(placement);
    if (iter != allowed_placements_.end())
        return;
    std::string message("placement of attribute '");
    message.append(attribute->name);
    message.append("' disallowed here");
    error_reporter->ReportError(attribute->location(), message);
}

void AttributeSchema::ValidateValue(ErrorReporter* error_reporter,
                                    const raw::Attribute* attribute) const {
    if (allowed_values_.size() == 0)
        return;
    auto iter = allowed_values_.find(attribute->value);
    if (iter != allowed_values_.end())
        return;
    std::string message("attribute '");
    message.append(attribute->name);
    message.append("' has invalid value '");
    message.append(attribute->value);
    message.append("', should be one of '");
    bool first = true;
    for (const auto& hint : allowed_values_) {
        if (!first)
            message.append(", ");
        message.append(hint);
        message.append("'");
        first = false;
    }
    error_reporter->ReportError(attribute->location(), message);
}

void AttributeSchema::ValidateConstraint(ErrorReporter* error_reporter,
                                         const raw::Attribute* attribute,
                                         const Decl* decl) const {
    auto check = error_reporter->Checkpoint();
    auto passed = constraint_(error_reporter, attribute, decl);
    if (passed) {
        assert(check.NoNewErrors() && "cannot add errors and pass");
    } else if (check.NoNewErrors()) {
        std::string message("declaration did not satisfy constraint of attribute '");
        message.append(attribute->name);
        message.append("' with value '");
        message.append(attribute->value);
        message.append("'");
        // TODO(pascallouis): It would be nicer to use the location of
        // the declaration, however we do not keep it around today.
        error_reporter->ReportError(attribute->location(), message);
    }
}

bool SimpleLayoutConstraint(ErrorReporter* error_reporter,
                            const raw::Attribute* attribute,
                            const Decl* decl) {
    assert(decl->kind == Decl::Kind::kStruct);
    auto struct_decl = static_cast<const Struct*>(decl);
    bool ok = true;
    for (const auto& member : struct_decl->members) {
        if (!IsSimple(member.type.get(), member.fieldshape)) {
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
                        const raw::Attribute* attribute,
                        const Decl* decl) {
    uint32_t bound;
    if (!ParseBound(error_reporter, attribute->location(), attribute->value, &bound))
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
        error_reporter->ReportError(attribute->location(), message.str());
        return false;
    }
    return true;
}

bool MaxHandlesConstraint(ErrorReporter* error_reporter,
                          const raw::Attribute* attribute,
                          const Decl* decl) {
    uint32_t bound;
    if (!ParseBound(error_reporter, attribute->location(), attribute->value.c_str(), &bound))
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
        error_reporter->ReportError(attribute->location(), message.str());
        return false;
    }
    return true;
}

Libraries::Libraries() {
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
    }, {
        /* any value */
    },
    MaxHandlesConstraint));
    AddAttributeSchema("Selector", AttributeSchema({
        AttributeSchema::Placement::kMethod,
    }, {
        /* any value */
    }));
    AddAttributeSchema("Transport", AttributeSchema({
        AttributeSchema::Placement::kInterfaceDecl,
    }, {
        "Channel",
        "SocketControl",
        "OvernetStream",
    }));
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
    size_t row1[s1_length+1];
    size_t row2[s1_length+1];
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
                last_row[i] + 1, this_row[i - 1] + 1), last_row[i - 1] + (s1c == s2c ? 0 : 1));
        }
        std::swap(last_row, this_row);
    }
    return last_row[s1_length];
}

const AttributeSchema* Libraries::RetrieveAttributeSchema(ErrorReporter* error_reporter,
                                                          const raw::Attribute* attribute) const {
    const auto& attribute_name = attribute->name;
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
            error_reporter->ReportWarning(attribute->location(), message);
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

bool Library::Fail(const SourceLocation& location, StringView message) {
    error_reporter_->ReportError(location, message);
    return false;
}

void Library::ValidateAttributesPlacement(AttributeSchema::Placement placement,
                                          const raw::AttributeList* attributes) {
    if (attributes == nullptr)
        return;
    for (const auto& attribute : attributes->attributes) {
        auto schema = all_libraries_->RetrieveAttributeSchema(error_reporter_, attribute.get());
        if (schema != nullptr) {
            schema->ValidatePlacement(error_reporter_, attribute.get(), placement);
            schema->ValidateValue(error_reporter_, attribute.get());
        }
    }
}

void Library::ValidateAttributesConstraints(const Decl* decl,
                                            const raw::AttributeList* attributes) {
    if (attributes == nullptr)
        return;
    for (const auto& attribute : attributes->attributes) {
        auto schema = all_libraries_->RetrieveAttributeSchema(nullptr, attribute.get());
        if (schema != nullptr)
            schema->ValidateConstraint(error_reporter_, attribute.get(), decl);
    }
}

Name Library::NextAnonymousName() {
    // TODO(pascallouis): Improve anonymous name generation. We want to be
    // specific about how these names are generated once they appear in the
    // JSON IR, and are exposed to the backends.
    std::ostringstream data;
    data << "SomeLongAnonymousPrefix";
    data << anon_counter_++;
    return Name(this, StringView(data.str()));
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

bool Library::ConsumeType(std::unique_ptr<raw::Type> raw_type, SourceLocation location,
                          std::unique_ptr<Type>* out_type) {
    switch (raw_type->kind) {
    case raw::Type::Kind::kArray: {
        auto array_type = static_cast<raw::ArrayType*>(raw_type.get());
        std::unique_ptr<Type> element_type;
        if (!ConsumeType(std::move(array_type->element_type), location, &element_type))
            return false;
        std::unique_ptr<Constant> element_count;
        if (!ConsumeConstant(std::move(array_type->element_count), location, &element_count))
            return false;
        *out_type = std::make_unique<ArrayType>(location, std::move(element_type),
                                                std::move(element_count));
        break;
    }
    case raw::Type::Kind::kVector: {
        auto vector_type = static_cast<raw::VectorType*>(raw_type.get());
        std::unique_ptr<Type> element_type;
        if (!ConsumeType(std::move(vector_type->element_type), location, &element_type))
            return false;
        std::unique_ptr<Constant> element_count = std::make_unique<SynthesizedConstant>(
            std::make_unique<Size>(Size::Max()));
        if (vector_type->maybe_element_count) {
            if (!ConsumeConstant(std::move(vector_type->maybe_element_count),
                                 location,
                                 &element_count))
                return false;
        }
        *out_type = std::make_unique<VectorType>(location, std::move(element_type),
                                                 std::move(element_count),
                                                 vector_type->nullability);
        break;
    }
    case raw::Type::Kind::kString: {
        auto string_type = static_cast<raw::StringType*>(raw_type.get());
        std::unique_ptr<Constant> element_count = std::make_unique<SynthesizedConstant>(
            std::make_unique<Size>(Size::Max()));
        if (string_type->maybe_element_count) {
            if (!ConsumeConstant(std::move(string_type->maybe_element_count),
                                 location,
                                 &element_count))
                return false;
        }
        *out_type =
            std::make_unique<StringType>(location, std::move(element_count),
                                         string_type->nullability);
        break;
    }
    case raw::Type::Kind::kHandle: {
        auto handle_type = static_cast<raw::HandleType*>(raw_type.get());
        *out_type = std::make_unique<HandleType>(handle_type->subtype, handle_type->nullability);
        break;
    }
    case raw::Type::Kind::kRequestHandle: {
        auto request_type = static_cast<raw::RequestHandleType*>(raw_type.get());
        Name name;
        if (!CompileCompoundIdentifier(request_type->identifier.get(), location, &name)) {
            return false;
        }
        *out_type = std::make_unique<RequestHandleType>(std::move(name), request_type->nullability);
        break;
    }
    case raw::Type::Kind::kIdentifier: {
        auto identifier_type = static_cast<raw::IdentifierType*>(raw_type.get());
        Name name;
        if (!CompileCompoundIdentifier(identifier_type->identifier.get(), location, &name)) {
            return false;
        }

        // Special case: primitives.
        const auto primitive_type = LookupPrimitiveType(name);
        if (primitive_type != nullptr) {
            if (identifier_type->nullability != types::Nullability::kNonnullable) {
                return Fail(raw_type->location(), "primitives cannot be nullable");
            }
            *out_type = std::make_unique<PrimitiveType>(*primitive_type);
            return true;
        }

        // Special case: type aliases.
        const auto alias_type = LookupTypeAlias(name);
        if (alias_type != nullptr) {
            if (identifier_type->nullability != types::Nullability::kNonnullable) {
                return Fail(raw_type->location(), "type aliases cannot be nullable");
            }
            *out_type = std::make_unique<PrimitiveType>(*alias_type);
            return true;
        }

        *out_type = std::make_unique<IdentifierType>(std::move(name), identifier_type->nullability);
        break;
    }
    }
    return true;
}

bool Library::ConsumeUsing(std::unique_ptr<raw::Using> using_directive) {
    if (using_directive->maybe_type)
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
    const auto& type_aliases = dep_library->type_aliases_;
    type_aliases_.insert(type_aliases.begin(), type_aliases.end());
    return true;
}

bool Library::ConsumeTypeAlias(std::unique_ptr<raw::Using> using_directive) {
    assert(using_directive->maybe_type);

    auto location = using_directive->using_path->components[0]->location();
    auto alias_name = Name(this, location);
    Name type_name;
    if (!CompileCompoundIdentifier(using_directive->maybe_type->identifier.get(),
                                   using_directive->maybe_type->location(),
                                   &type_name)) {
        return false;
    }
    auto primitive_type = LookupPrimitiveType(type_name);
    if (primitive_type == nullptr) {
        std::string message("may only alias primitive types, found ");
        message += NameName(type_name, ".", "/");
        return Fail(location, message);
    }
    auto using_dir = std::make_unique<Using>(std::move(alias_name), primitive_type);
    type_aliases_.emplace(&using_dir->name, using_dir.get());
    using_.push_back(std::move(using_dir));
    return true;
}

bool Library::ConsumeConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const_declaration) {
    auto attributes = std::move(const_declaration->attributes);
    auto location = const_declaration->identifier->location();
    auto name = Name(this, location);
    std::unique_ptr<Type> type;
    if (!ConsumeType(std::move(const_declaration->type), location, &type))
        return false;

    std::unique_ptr<Constant> constant;
    if (!ConsumeConstant(std::move(const_declaration->constant), location, &constant))
        return false;

    const_declarations_.push_back(std::make_unique<Const>(std::move(attributes), std::move(name),
                                                          std::move(type), std::move(constant)));
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
        auto attributes = std::move(member->attributes);
        members.emplace_back(location, std::move(value), std::move(attributes));
        // TODO(pascallouis): right now, members are not registered. Look into
        // registering them, potentially under the enum name qualifier such as
        // <name_of_enum>.<name_of_member>.
    }

    enum_declarations_.push_back(std::make_unique<Enum>(
        std::move(enum_declaration->attributes),
        Name(this, enum_declaration->identifier->location()),
        std::move(enum_declaration->maybe_subtype),
        std::move(members)));
    if (!RegisterDecl(enum_declarations_.back().get()))
        return false;

    return true;
}

bool Library::ConsumeInterfaceDeclaration(
    std::unique_ptr<raw::InterfaceDeclaration> interface_declaration) {
    auto attributes = std::move(interface_declaration->attributes);
    auto name = Name(this, interface_declaration->identifier->location());

    std::vector<Name> superinterfaces;
    for (auto& superinterface : interface_declaration->superinterfaces) {
        Name superinterface_name;
        auto location = superinterface->components[0]->location();
        if (!CompileCompoundIdentifier(superinterface.get(), location, &superinterface_name)) {
            return false;
        }
        superinterfaces.push_back(std::move(superinterface_name));
    }

    std::vector<Interface::Method> methods;
    for (auto& method : interface_declaration->methods) {
        std::unique_ptr<raw::Ordinal> ordinal_literal =
            std::make_unique<raw::Ordinal>(fidl::ordinals::GetOrdinal(library_name_, name.name_part(), *method));

        auto attributes = std::move(method->attributes);
        SourceLocation method_name = method->identifier->location();

        Struct* maybe_request = nullptr;
        if (method->maybe_request != nullptr) {
            if (!ConsumeParameterList(std::move(method->maybe_request), &maybe_request))
                return false;
        }

        Struct* maybe_response = nullptr;
        if (method->maybe_response != nullptr) {
            if (!ConsumeParameterList(std::move(method->maybe_response), &maybe_response))
                return false;
        }

        assert(maybe_request != nullptr || maybe_response != nullptr);
        methods.emplace_back(std::move(attributes),
                             std::move(ordinal_literal),
                             std::make_unique<raw::Ordinal>(fidl::ordinals::GetGeneratedOrdinal(library_name_, name.name_part(), *method)),
                             std::move(method_name), std::move(maybe_request),
                             std::move(maybe_response));
    }

    interface_declarations_.push_back(
        std::make_unique<Interface>(std::move(attributes), std::move(name),
                                    std::move(superinterfaces), std::move(methods)));
    return RegisterDecl(interface_declarations_.back().get());
}

bool Library::ConsumeParameterList(std::unique_ptr<raw::ParameterList> parameter_list,
                                   Struct** out_struct_decl) {
    std::vector<Struct::Member> members;
    for (auto& parameter : parameter_list->parameter_list) {
        const SourceLocation name = parameter->identifier->location();
        std::unique_ptr<Type> type;
        if (!ConsumeType(std::move(parameter->type), name, &type))
            return false;
        members.emplace_back(
            std::move(type), name,
            nullptr /* maybe_default_value */,
            nullptr /* attributes */);
    }

    auto name = NextAnonymousName();
    struct_declarations_.push_back(
        std::make_unique<Struct>(nullptr /* attributes */, std::move(name), std::move(members),
                                 true /* anonymous */));

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
        std::unique_ptr<Type> type;
        auto location = member->identifier->location();
        if (!ConsumeType(std::move(member->type), location, &type))
            return false;
        std::unique_ptr<Constant> maybe_default_value;
        if (member->maybe_default_value != nullptr) {
            if (!ConsumeConstant(std::move(member->maybe_default_value), location,
                                 &maybe_default_value))
                return false;
        }
        auto attributes = std::move(member->attributes);
        members.emplace_back(std::move(type), member->identifier->location(),
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
            std::unique_ptr<Type> type;
            if (!ConsumeType(std::move(member->maybe_used->type), member->location(), &type))
                return false;
            std::unique_ptr<Constant> maybe_default_value;
            if (member->maybe_used->maybe_default_value != nullptr) {
                if (!ConsumeConstant(std::move(member->maybe_used->maybe_default_value),
                                     member->location(), &maybe_default_value))
                    return false;
            }
            if (type->nullability != types::Nullability::kNonnullable) {
                return Fail(member->location(), "Table members cannot be nullable");
            }
            auto attributes = std::move(member->maybe_used->attributes);
            members.emplace_back(std::move(ordinal_literal), std::move(type),
                                 member->maybe_used->identifier->location(),
                                 std::move(maybe_default_value), std::move(attributes));
        } else {
            members.emplace_back(std::move(ordinal_literal));
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
        std::unique_ptr<Type> type;
        if (!ConsumeType(std::move(member->type), location, &type))
            return false;
        auto attributes = std::move(member->attributes);
        members.emplace_back(std::move(type), location, std::move(attributes));
    }

    auto attributes = std::move(union_declaration->attributes);
    auto name = Name(this, union_declaration->identifier->location());

    union_declarations_.push_back(
        std::make_unique<Union>(std::move(attributes), std::move(name), std::move(members)));
    return RegisterDecl(union_declarations_.back().get());
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
        if (!TypeIsConvertibleTo(const_decl->type.get(), type))
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
    msg_stream << NameFlatType(const_decl->type.get());
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

        if (!ResolveConstant(string_type->max_size.get(), &kSizeType))
            return false;

        auto max_size = static_cast<const Size&>(string_type->max_size->Value());
        // TODO(pascallouis): because data() contains the raw content,
        // with the two " to identify strings, we need to take this
        // into account. We should expose the actual size of string
        // literals properly, and take into account escaping.
        uint64_t string_size = string_data.size() - 2;
        if (max_size.value < string_size) {
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

const Type* Library::TypeResolve(const Type* type) {
    if (type->kind != flat::Type::Kind::kIdentifier) {
        return type;
    }
    auto identifier_type = static_cast<const flat::IdentifierType*>(type);
    auto decl = LookupDeclByType(identifier_type, LookupOption::kIgnoreNullable);
    switch (decl->kind) {
    case Decl::Kind::kEnum: {
        // TODO(pascallouis): by circumventing enum types like this, we're
        // preventing a more precise handling of enum types, and in particular
        // forbidding that out-of-range values be assignable to this enum in
        // const declarations.
        auto enum_decl = static_cast<const flat::Enum*>(decl);
        return enum_decl->type;
    }
    default:
        return type;
    } // switch
}

bool Library::TypeIsConvertibleTo(const Type* from_type, const Type* to_type) {
    from_type = TypeResolve(from_type);
    to_type = TypeResolve(to_type);

    switch (to_type->kind) {
    case flat::Type::Kind::kString: {
        if (from_type->kind != flat::Type::Kind::kString) {
            return false;
        }

        auto from_string_type = static_cast<const flat::StringType*>(from_type);
        auto to_string_type = static_cast<const flat::StringType*>(to_type);

        if (to_string_type->nullability == types::Nullability::kNonnullable &&
            from_string_type->nullability != types::Nullability::kNonnullable) {
            return false;
        }

        auto to_string_size = static_cast<const Size&>(to_string_type->max_size->Value());
        auto from_string_size = static_cast<const Size&>(from_string_type->max_size->Value());
        if (to_string_size < from_string_size) {
            return false;
        }

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

Decl* Library::LookupConstant(const Type* type, const Name& name) {
    auto decl = LookupDeclByType(type, LookupOption::kIgnoreNullable);
    if (decl == nullptr) {
        // This wasn't a named type. Thus we are looking up a
        // top-level constant, of string or primitive type.
        assert(type->kind == Type::Kind::kString || type->kind == Type::Kind::kPrimitive);
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

const PrimitiveType* Library::LookupPrimitiveType(const Name& name) const {
    auto type = typespace_->Lookup(name, types::Nullability::kNonnullable,
                                   Typespace::LookupMode::kNoForwardReferences);
    if (type == nullptr)
        return nullptr;
    if (type->kind != Type::Kind::kPrimitive)
        return nullptr;
    return static_cast<PrimitiveType*>(type);
}

const PrimitiveType* Library::LookupTypeAlias(const Name& name) const {
    const auto it = type_aliases_.find(&name);
    if (it == type_aliases_.end())
        return nullptr;
    return it->second->type;
}

Decl* Library::LookupDeclByType(const Type* type, LookupOption option) const {
    for (;;) {
        switch (type->kind) {
        case flat::Type::Kind::kString:
        case flat::Type::Kind::kHandle:
        case flat::Type::Kind::kRequestHandle:
        case flat::Type::Kind::kPrimitive:
            return nullptr;
        case flat::Type::Kind::kVector: {
            type = static_cast<const flat::VectorType*>(type)->element_type.get();
            continue;
        }
        case flat::Type::Kind::kArray: {
            type = static_cast<const flat::ArrayType*>(type)->element_type.get();
            continue;
        }
        case flat::Type::Kind::kIdentifier: {
            auto identifier_type = static_cast<const flat::IdentifierType*>(type);
            if (identifier_type->nullability == types::Nullability::kNullable && option == LookupOption::kIgnoreNullable) {
                return nullptr;
            }
            return LookupDeclByName(identifier_type->name);
        }
        }
    }
}

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

// An edge from D1 to D2 means that a C needs to see the declaration
// of D1 before the declaration of D2. For instance, given the fidl
//     struct D2 { D1 d; };
//     struct D1 { int32 x; };
// D1 has an edge pointing to D2. Note that struct and union pointers,
// unlike inline structs or unions, do not have dependency edges.
bool Library::DeclDependencies(Decl* decl, std::set<Decl*>* out_edges) {
    std::set<Decl*> edges;
    auto maybe_add_decl = [this, &edges](const Type* type, LookupOption option) {
        auto type_decl = LookupDeclByType(type, option);
        if (type_decl != nullptr) {
            edges.insert(type_decl);
        }
    };
    auto maybe_add_name = [this, &edges](const Name& name) {
        auto type_decl = LookupDeclByName(name);
        if (type_decl != nullptr) {
            edges.insert(type_decl);
        }
    };
    auto maybe_add_constant = [this, &edges](const Type* type, const Constant* constant) -> bool {
        switch (constant->kind) {
        case Constant::Kind::kIdentifier: {
            auto identifier = static_cast<const flat::IdentifierConstant*>(constant);
            auto decl = LookupConstant(type, identifier->name);
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
    case Decl::Kind::kConst: {
        auto const_decl = static_cast<const Const*>(decl);
        if (!maybe_add_constant(const_decl->type.get(), const_decl->value.get()))
            return false;
        break;
    }
    case Decl::Kind::kEnum: {
        break;
    }
    case Decl::Kind::kInterface: {
        auto interface_decl = static_cast<const Interface*>(decl);
        for (const auto& superinterface : interface_decl->superinterfaces) {
            maybe_add_name(superinterface);
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
            auto option = struct_decl->anonymous ? LookupOption::kIncludeNullable
                                                 : LookupOption::kIgnoreNullable;
            maybe_add_decl(member.type.get(), option);
            if (member.maybe_default_value) {
                if (!maybe_add_constant(member.type.get(), member.maybe_default_value.get()))
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
            maybe_add_decl(member.maybe_used->type.get(), LookupOption::kIgnoreNullable);
            if (member.maybe_used->maybe_default_value) {
                if (!maybe_add_constant(member.maybe_used->type.get(),
                                        member.maybe_used->maybe_default_value.get()))
                    return false;
            }
        }
        break;
    }
    case Decl::Kind::kUnion: {
        auto union_decl = static_cast<const Union*>(decl);
        for (const auto& member : union_decl->members) {
            maybe_add_decl(member.type.get(), LookupOption::kIgnoreNullable);
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

bool Library::CompileConst(Const* const_declaration) {
    if (const_declaration->compiled)
        return true;

    Compiling guard(const_declaration);
    TypeShape typeshape;
    if (!CompileType(const_declaration->type.get(), &typeshape))
        return false;
    const auto* const_type = const_declaration->type.get();
    if (!TypeCanBeConst(const_type)) {
        std::ostringstream msg_stream;
        msg_stream << "invalid constant type " << NameFlatType(const_type);
        return Fail(*const_declaration, msg_stream.str());
    }
    if (!ResolveConstant(const_declaration->value.get(), const_type))
        return Fail(*const_declaration, "unable to resolve constant value");

    // Attributes: for const declarations, we only check placement.
    ValidateAttributesPlacement(
        AttributeSchema::Placement::kConstDecl, const_declaration->attributes.get());

    return true;
}

bool Library::CompileEnum(Enum* enum_declaration) {
    Compiling guard(enum_declaration);

    Name subtype_name(nullptr, "uint32");
    if (enum_declaration->maybe_subtype) {
        auto location = enum_declaration->maybe_subtype->location();
        if (!CompileCompoundIdentifier(enum_declaration->maybe_subtype->identifier.get(),
                                       location, &subtype_name)) {
            return false;
        }
    }

    auto primitive_type = LookupPrimitiveType(subtype_name);
    if (primitive_type == nullptr) {
        std::string message("enums may only be of integral primitive type, found ");
        message += NameName(subtype_name, ".", "/");
        return Fail(*enum_declaration, message);
    }

    enum_declaration->type = primitive_type;
    enum_declaration->typeshape = PrimitiveTypeShape(primitive_type->subtype);

    // Validate constants.
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
        message += NameName(subtype_name, ".", "/");
        return Fail(*enum_declaration, message);
    }

    auto placement_ok = error_reporter_->Checkpoint();
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

    return true;
}

bool HasSimpleLayout(const Decl* decl) {
    return decl->GetAttribute("Layout") == "Simple";
}

bool Library::CompileInterface(Interface* interface_declaration) {
    Compiling guard(interface_declaration);
    MethodScope method_scope;
    auto CheckScopes = [this, &interface_declaration, &method_scope](const Interface* interface, auto Visitor) -> bool {
        for (const auto& name : interface->superinterfaces) {
            auto decl = LookupDeclByName(name);
            if (decl == nullptr)
                return Fail(name, "There is no declaration with this name");
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
            assert(!superinterface->name.is_anonymous());
            if (method_scope.interfaces.Insert(superinterface, superinterface->name.source_location()).ok()) {
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
                                name_result.previous_occurance().position());
            auto ordinal_result = method_scope.ordinals.Insert(method.ordinal->value, method.name);
            if (method.ordinal->value == 0)
                return Fail(method.ordinal->location(), "Ordinal value 0 disallowed.");
            if (!ordinal_result.ok()) {
                std::string replacement_method(
                    fidl::ordinals::GetSelector(method.attributes.get(), method.name));
                replacement_method.push_back('_');
                return Fail(method.ordinal->location(),
                            "Multiple methods with the same ordinal in an interface; previous was at " +
                                ordinal_result.previous_occurance().position() + ". If these " +
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

    // Beware, hacky solution: method request and response are structs, whose
    // typeshape is computed separately. However, in the JSON IR, we add 16 bytes
    // to request and response to account for the header size (and ensure the
    // alignment is at least 4 bytes). Now that we represent request and responses
    // as structs, we should represent this differently in the JSON IR, and let the
    // backends figure this out, or better yet, describe the header directly.
    //
    // For now though, due to topological sort, the code below will always overwrite
    // the previous calculation of the typeshape for these request and response
    // struct, hence keeping the behavior of the compiler unchanged.
    for (auto& method : interface_declaration->methods) {
        auto CreateMessage = [&](Struct* message) -> bool {
            Scope<StringView> scope;
            auto header_field_shape = FieldShape(TypeShape(16u, 4u));
            std::vector<FieldShape*> message_struct;
            message_struct.push_back(&header_field_shape);
            for (auto& param : message->members) {
                if (!scope.Insert(param.name.data(), param.name).ok())
                    return Fail(param.name, "Multiple parameters with the same name in a method");
                if (!CompileType(param.type.get(), &param.fieldshape.Typeshape()))
                    return false;
                message_struct.push_back(&param.fieldshape);
            }
            message->typeshape = FidlMessageTypeShape(&message_struct);
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

    auto placement_ok = error_reporter_->Checkpoint();
    // Attributes: check placement.
    ValidateAttributesPlacement(
        AttributeSchema::Placement::kInterfaceDecl,
        interface_declaration->attributes.get());
    for (const auto& method : interface_declaration->methods) {
        ValidateAttributesPlacement(
            AttributeSchema::Placement::kMethod,
            method.attributes.get());
    }
    if (placement_ok.NoNewErrors()) {
        // Attributes: check constraints.
        for (const auto& method : interface_declaration->methods) {
            if (method.maybe_request) {
                ValidateAttributesConstraints(
                    method.maybe_request,
                    interface_declaration->attributes.get());
                ValidateAttributesConstraints(
                    method.maybe_request,
                    method.attributes.get());
            }
            if (method.maybe_response) {
                ValidateAttributesConstraints(
                    method.maybe_response,
                    interface_declaration->attributes.get());
                ValidateAttributesConstraints(
                    method.maybe_response,
                    method.attributes.get());
            }
        }
    }

    return true;
}

bool Library::CompileStruct(Struct* struct_declaration) {
    Compiling guard(struct_declaration);
    Scope<StringView> scope;
    std::vector<FieldShape*> fidl_struct;

    uint32_t max_member_handles = 0;
    for (auto& member : struct_declaration->members) {
        auto name_result = scope.Insert(member.name.data(), member.name);
        if (!name_result.ok())
            return Fail(member.name,
                        "Multiple struct fields with the same name; previous was at " +
                            name_result.previous_occurance().position());
        if (!CompileType(member.type.get(), &member.fieldshape.Typeshape()))
            return false;
        fidl_struct.push_back(&member.fieldshape);
    }

    if (struct_declaration->recursive) {
        max_member_handles = std::numeric_limits<uint32_t>::max();
    } else {
        // Member handles will be counted by CStructTypeShape.
        max_member_handles = 0;
    }

    struct_declaration->typeshape = CStructTypeShape(&fidl_struct, max_member_handles);

    auto placement_ok = error_reporter_->Checkpoint();
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

    return true;
}

bool Library::CompileTable(Table* table_declaration) {
    Compiling guard(table_declaration);
    Scope<StringView> name_scope;
    Scope<uint32_t> ordinal_scope;

    uint32_t max_member_handles = 0;
    for (auto& member : table_declaration->members) {
        auto ordinal_result = ordinal_scope.Insert(member.ordinal->value, member.ordinal->location());
        if (!ordinal_result.ok())
            return Fail(member.ordinal->location(),
                        "Multiple table fields with the same ordinal; previous was at " +
                            ordinal_result.previous_occurance().position());
        if (member.maybe_used) {
            auto name_result = name_scope.Insert(member.maybe_used->name.data(), member.maybe_used->name);
            if (!name_result.ok())
                return Fail(member.maybe_used->name,
                            "Multiple table fields with the same name; previous was at " +
                                name_result.previous_occurance().position());
            if (!CompileType(member.maybe_used->type.get(), &member.maybe_used->typeshape))
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

    table_declaration->typeshape = CTableTypeShape(&fields, max_member_handles);

    // Attributes: check placement.
    auto placement_ok = error_reporter_->Checkpoint();
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

    return true;
}

bool Library::CompileUnion(Union* union_declaration) {
    Compiling guard(union_declaration);
    Scope<StringView> scope;
    for (auto& member : union_declaration->members) {
        auto name_result = scope.Insert(member.name.data(), member.name);
        if (!name_result.ok())
            return Fail(member.name,
                        "Multiple union members with the same name; previous was at " +
                            name_result.previous_occurance().position());
        if (!CompileType(member.type.get(), &member.fieldshape.Typeshape()))
            return false;
    }

    auto tag = FieldShape(kUint32TypeShape);
    union_declaration->membershape = FieldShape(CUnionTypeShape(union_declaration->members));
    uint32_t extra_handles = 0;
    if (union_declaration->recursive && union_declaration->membershape.MaxHandles()) {
        extra_handles = std::numeric_limits<uint32_t>::max();
    }
    std::vector<FieldShape*> fidl_union = {&tag, &union_declaration->membershape};
    union_declaration->typeshape = CStructTypeShape(&fidl_union, extra_handles);

    // This is either 4 or 8, depending on whether any union members
    // have alignment 8.
    auto offset = union_declaration->membershape.Offset();
    for (auto& member : union_declaration->members) {
        member.fieldshape.SetOffset(offset);
    }

    auto placement_ok = error_reporter_->Checkpoint();
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
        switch (decl->kind) {
        case Decl::Kind::kConst: {
            auto const_decl = static_cast<Const*>(decl);
            if (!CompileConst(const_decl)) {
                return false;
            }
            break;
        }
        case Decl::Kind::kEnum: {
            auto enum_decl = static_cast<Enum*>(decl);
            if (!CompileEnum(enum_decl)) {
                return false;
            }
            break;
        }
        case Decl::Kind::kInterface: {
            auto interface_decl = static_cast<Interface*>(decl);
            if (!CompileInterface(interface_decl)) {
                return false;
            }
            break;
        }
        case Decl::Kind::kStruct: {
            auto struct_decl = static_cast<Struct*>(decl);
            if (!CompileStruct(struct_decl)) {
                return false;
            }
            break;
        }
        case Decl::Kind::kTable: {
            auto table_decl = static_cast<Table*>(decl);
            if (!CompileTable(table_decl)) {
                return false;
            }
            break;
        }
        case Decl::Kind::kUnion: {
            auto union_decl = static_cast<Union*>(decl);
            if (!CompileUnion(union_decl)) {
                return false;
            }
            break;
        }
        default:
            abort();
        }
        assert(!decl->compiling);
        assert(decl->compiled);
    }

    return error_reporter_->errors().size() == 0;
}

bool Library::CompileArrayType(flat::ArrayType* array_type, TypeShape* out_typeshape) {
    TypeShape element_typeshape;
    if (!CompileType(array_type->element_type.get(), &element_typeshape))
        return false;

    // Resolve element count bound now that all constant identifiers have been consumed
    if (!ResolveConstant(array_type->element_count.get(), &kSizeType))
        return Fail(array_type->name, "unable to parse size bound for array");

    assert(array_type->element_count->Value().kind == ConstantValue::Kind::kUint32);
    auto element_count = static_cast<const Size&>(array_type->element_count->Value());
    TypeShape typeshape = ArrayTypeShape(element_typeshape, element_count.value);

    // Now that the array element type is compiled and the size is resolved, set the array size.
    array_type->size = typeshape.Size();
    *out_typeshape = typeshape;
    return true;
}

bool Library::CompileVectorType(flat::VectorType* vector_type, TypeShape* out_typeshape) {
    // All we need from the element typeshape is the maximum number of handles.
    TypeShape element_typeshape;
    if (!CompileType(vector_type->element_type.get(), &element_typeshape))
        return false;

    // Resolve element count bound now that all constant identifiers have been consumed
    if (!ResolveConstant(vector_type->element_count.get(), &kSizeType))
        return Fail(vector_type->name, "unable to parse size bound for vector");

    assert(vector_type->element_count->Value().kind == ConstantValue::Kind::kUint32);
    auto element_count = static_cast<const Size&>(vector_type->element_count->Value());
    *out_typeshape = VectorTypeShape(element_typeshape, element_count.value);

    return true;
}

bool Library::CompileStringType(flat::StringType* string_type, TypeShape* out_typeshape) {
    // Resolve max size bound now that all constant identifiers have been consumed
    if (!ResolveConstant(string_type->max_size.get(), &kSizeType))
        return Fail(string_type->name, "unable to parse size bound for string");

    assert(string_type->max_size->Value().kind == ConstantValue::Kind::kUint32);
    auto max_size = static_cast<const Size&>(string_type->max_size->Value());
    *out_typeshape = StringTypeShape(max_size.value);
    return true;
}

bool Library::CompileHandleType(flat::HandleType* handle_type, TypeShape* out_typeshape) {
    // Nothing to check.
    *out_typeshape = kHandleTypeShape;
    return true;
}

bool Library::CompileRequestHandleType(flat::RequestHandleType* request_type,
                                       TypeShape* out_typeshape) {
    auto named_decl = LookupDeclByName(request_type->name);
    if (!named_decl || named_decl->kind != Decl::Kind::kInterface) {
        std::string message = "Undefined reference \"";
        message.append(request_type->name.name_part());
        message.append("\" in request handle name");
        return Fail(request_type->name, message);
    }

    *out_typeshape = kHandleTypeShape;
    return true;
}

bool Library::CompilePrimitiveType(flat::PrimitiveType* primitive_type, TypeShape* out_typeshape) {
    *out_typeshape = PrimitiveTypeShape(primitive_type->subtype);
    return true;
}

bool Library::CompileIdentifierType(flat::IdentifierType* identifier_type,
                                    TypeShape* out_typeshape) {
    TypeShape typeshape;

    auto named_decl = LookupDeclByName(identifier_type->name);
    if (!named_decl) {
        std::string message("Undefined reference \"");
        message.append(identifier_type->name.name_part());
        message.append("\" in identifier type name");
        return Fail(identifier_type->name, message);
    }

    switch (named_decl->kind) {
    case Decl::Kind::kConst: {
        // A constant isn't a type!
        return Fail(identifier_type->name,
                    "The name of a constant was used where a type was expected");
    }
    case Decl::Kind::kEnum: {
        if (identifier_type->nullability == types::Nullability::kNullable) {
            // Enums aren't nullable!
            return Fail(identifier_type->name, "An enum was referred to as 'nullable'");
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
        Struct* struct_decl = static_cast<Struct*>(named_decl);
        if (!struct_decl->compiled) {
            if (struct_decl->compiling) {
                struct_decl->recursive = true;
            } else {
                if (!CompileStruct(struct_decl)) {
                    return false;
                }
            }
        }
        typeshape = struct_decl->typeshape;
        if (identifier_type->nullability == types::Nullability::kNullable)
            typeshape = PointerTypeShape(typeshape);
        break;
    }
    case Decl::Kind::kTable: {
        Table* table_decl = static_cast<Table*>(named_decl);
        if (!table_decl->compiled) {
            if (table_decl->compiling) {
                table_decl->recursive = true;
            } else {
                if (!CompileTable(table_decl)) {
                    return false;
                }
            }
        }
        typeshape = table_decl->typeshape;
        if (identifier_type->nullability == types::Nullability::kNullable)
            typeshape = PointerTypeShape(typeshape);
        break;
    }
    case Decl::Kind::kUnion: {
        Union* union_decl = static_cast<Union*>(named_decl);
        if (!union_decl->compiled) {
            if (union_decl->compiling) {
                union_decl->recursive = true;
            } else {
                if (!CompileUnion(union_decl)) {
                    return false;
                }
            }
        }
        typeshape = union_decl->typeshape;
        if (identifier_type->nullability == types::Nullability::kNullable)
            typeshape = PointerTypeShape(typeshape);
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

bool Library::CompileType(Type* type, TypeShape* out_typeshape) {
    switch (type->kind) {
    case Type::Kind::kArray: {
        auto array_type = static_cast<ArrayType*>(type);
        return CompileArrayType(array_type, out_typeshape);
    }

    case Type::Kind::kVector: {
        auto vector_type = static_cast<VectorType*>(type);
        return CompileVectorType(vector_type, out_typeshape);
    }

    case Type::Kind::kString: {
        auto string_type = static_cast<StringType*>(type);
        return CompileStringType(string_type, out_typeshape);
    }

    case Type::Kind::kHandle: {
        auto handle_type = static_cast<HandleType*>(type);
        return CompileHandleType(handle_type, out_typeshape);
    }

    case Type::Kind::kRequestHandle: {
        auto request_type = static_cast<RequestHandleType*>(type);
        return CompileRequestHandleType(request_type, out_typeshape);
    }

    case Type::Kind::kPrimitive: {
        auto primitive_type = static_cast<PrimitiveType*>(type);
        return CompilePrimitiveType(primitive_type, out_typeshape);
    }

    case Type::Kind::kIdentifier: {
        auto identifier_type = static_cast<IdentifierType*>(type);
        return CompileIdentifierType(identifier_type, out_typeshape);
    }
    }
}

template <typename MemberType>
bool Library::ValidateEnumMembers(Enum* enum_decl) {
    static_assert(std::is_integral<MemberType>::value && !std::is_same<MemberType, bool>::value,
                  "Enum members must be an integral type!");
    assert(enum_decl != nullptr);

    Scope<std::string> name_scope;
    Scope<MemberType> value_scope;
    bool success = true;
    for (auto& member : enum_decl->members) {
        assert(member.value != nullptr && "Compiler bug: enum member value is null!");

        if (!ResolveConstant(member.value.get(), enum_decl->type))
            return Fail(member.name, "unable to resolve struct member");

        // Check that the enum member identifier hasn't been used yet
        std::string name = NameIdentifier(member.name);
        auto name_result = name_scope.Insert(name, member.name);
        if (!name_result.ok()) {
            std::ostringstream msg_stream;
            msg_stream << "name of member " << name;
            msg_stream << " conflicts with previously declared member in the enum ";
            msg_stream << enum_decl->GetName();

            // We can log the error and then continue validating for other issues in the enum
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
            msg_stream << NameIdentifier(value_result.previous_occurance()) << " in the enum ";
            msg_stream << enum_decl->GetName();

            // We can log the error and then continue validating other members for other bugs
            success = Fail(member.name, msg_stream.str());
        }
    }

    return success;
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
