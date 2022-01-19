// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat_ast.h"

#include "fidl/flat/attribute_schema.h"
#include "fidl/flat/compile_step.h"
#include "fidl/flat/consume_step.h"
#include "fidl/flat/sort_step.h"
#include "fidl/flat/verify_steps.h"
#include "fidl/flat/visitor.h"

namespace fidl::flat {

using namespace diagnostics;

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

std::string Decl::GetName() const { return std::string(name.decl_name()); }

const std::set<std::pair<std::string, std::string_view>> allowed_simple_unions{{
    {"fuchsia.io", "NodeInfo"},
}};

bool IsSimple(const Type* type, Reporter* reporter) {
  auto depth = fidl::OldWireFormatDepth(type);
  switch (type->kind) {
    case Type::Kind::kVector: {
      auto vector_type = static_cast<const VectorType*>(type);
      if (*vector_type->element_count == Size::Max())
        return false;
      switch (vector_type->element_type->kind) {
        case Type::Kind::kHandle:
        case Type::Kind::kTransportSide:
        case Type::Kind::kPrimitive:
          return true;
        case Type::Kind::kArray:
        case Type::Kind::kVector:
        case Type::Kind::kString:
        case Type::Kind::kIdentifier:
        case Type::Kind::kBox:
          return false;
        case Type::Kind::kUntypedNumeric:
          assert(false && "compiler bug: should not have untyped numeric here");
          return false;
      }
    }
    case Type::Kind::kString: {
      auto string_type = static_cast<const StringType*>(type);
      return *string_type->max_size < Size::Max();
    }
    case Type::Kind::kArray:
    case Type::Kind::kHandle:
    case Type::Kind::kTransportSide:
    case Type::Kind::kPrimitive:
      return depth == 0u;
    case Type::Kind::kIdentifier: {
      auto identifier_type = static_cast<const IdentifierType*>(type);
      if (identifier_type->type_decl->kind == Decl::Kind::kUnion) {
        auto union_name = std::make_pair<const std::string&, const std::string_view&>(
            LibraryName(identifier_type->name.library(), "."), identifier_type->name.decl_name());
        if (allowed_simple_unions.find(union_name) == allowed_simple_unions.end()) {
          // Any unions not in the allow-list are treated as non-simple.
          return reporter->Fail(ErrUnionCannotBeSimple, identifier_type->name.span().value(),
                                identifier_type->name);
        }
      }
      // TODO(fxbug.dev/70186): This only applies to nullable structs, which should
      // be handled as box.
      switch (identifier_type->nullability) {
        case types::Nullability::kNullable:
          // If the identifier is nullable, then we can handle a depth of 1
          // because the secondary object is directly accessible.
          return depth <= 1u;
        case types::Nullability::kNonnullable:
          return depth == 0u;
      }
    }
    case Type::Kind::kBox:
      // we can handle a depth of 1 because the secondary object is directly accessible.
      return depth <= 1u;
    case Type::Kind::kUntypedNumeric:
      assert(false && "compiler bug: should not have untyped numeric here");
      return false;
  }
}

FieldShape Struct::Member::fieldshape(WireFormat wire_format) const {
  return FieldShape(*this, wire_format);
}

FieldShape Table::Member::Used::fieldshape(WireFormat wire_format) const {
  return FieldShape(*this, wire_format);
}

FieldShape Union::Member::Used::fieldshape(WireFormat wire_format) const {
  return FieldShape(*this, wire_format);
}

std::vector<std::reference_wrapper<const Union::Member>> Union::MembersSortedByXUnionOrdinal()
    const {
  std::vector<std::reference_wrapper<const Member>> sorted_members(members.cbegin(),
                                                                   members.cend());
  std::sort(sorted_members.begin(), sorted_members.end(),
            [](const auto& member1, const auto& member2) {
              return member1.get().ordinal->value < member2.get().ordinal->value;
            });
  return sorted_members;
}

bool SimpleLayoutConstraint(Reporter* reporter, const Attribute* attr, const Element* element) {
  assert(element);
  bool ok = true;
  switch (element->kind) {
    case Element::Kind::kProtocol: {
      auto protocol = static_cast<const Protocol*>(element);
      for (const auto& method_with_info : protocol->all_methods) {
        auto* method = method_with_info.method;
        if (!SimpleLayoutConstraint(reporter, attr, method)) {
          ok = false;
        }
      }
      break;
    }
    case Element::Kind::kProtocolMethod: {
      auto method = static_cast<const Protocol::Method*>(element);
      if (method->maybe_request) {
        auto id = static_cast<const flat::IdentifierType*>(method->maybe_request->type);

        // TODO(fxbug.dev/88343): switch on union/table when those are enabled.
        auto as_struct = static_cast<const flat::Struct*>(id->type_decl);
        if (!SimpleLayoutConstraint(reporter, attr, as_struct)) {
          ok = false;
        }
      }
      if (method->maybe_response) {
        auto id = static_cast<const flat::IdentifierType*>(method->maybe_response->type);

        // TODO(fxbug.dev/88343): switch on union/table when those are enabled.
        auto as_struct = static_cast<const flat::Struct*>(id->type_decl);
        if (!SimpleLayoutConstraint(reporter, attr, as_struct)) {
          ok = false;
        }
      }
      break;
    }
    case Element::Kind::kStruct: {
      auto struct_decl = static_cast<const Struct*>(element);
      for (const auto& member : struct_decl->members) {
        if (!IsSimple(member.type_ctor->type, reporter)) {
          reporter->Fail(ErrMemberMustBeSimple, member.name, member.name.data());
          ok = false;
        }
      }
      break;
    }
    default:
      assert(false && "unexpected kind");
  }
  return ok;
}

bool ParseBound(Reporter* reporter, const Attribute* attribute, std::string_view input,
                uint32_t* out_value) {
  auto result = utils::ParseNumeric(input, out_value, 10);
  switch (result) {
    case utils::ParseNumericResult::kOutOfBounds:
      reporter->Fail(ErrBoundIsTooBig, attribute->span, attribute, input);
      return false;
    case utils::ParseNumericResult::kMalformed: {
      reporter->Fail(ErrUnableToParseBound, attribute->span, attribute, input);
      return false;
    }
    case utils::ParseNumericResult::kSuccess:
      return true;
  }
}

bool MaxBytesConstraint(Reporter* reporter, const Attribute* attribute, const Element* element) {
  assert(element);
  auto arg = attribute->GetArg(AttributeArg::kDefaultAnonymousName);
  auto arg_value = static_cast<const flat::StringConstantValue&>(arg->value->Value());

  uint32_t bound;
  if (!ParseBound(reporter, attribute, std::string(arg_value.MakeContents()), &bound))
    return false;
  uint32_t max_bytes = std::numeric_limits<uint32_t>::max();
  switch (element->kind) {
    case Element::Kind::kProtocol: {
      auto protocol = static_cast<const Protocol*>(element);
      bool ok = true;
      for (const auto& method_with_info : protocol->all_methods) {
        auto* method = method_with_info.method;
        if (!MaxBytesConstraint(reporter, attribute, method)) {
          ok = false;
        }
      }
      return ok;
    }
    case Element::Kind::kProtocolMethod: {
      auto method = static_cast<const Protocol::Method*>(element);
      bool ok = true;
      if (method->maybe_request) {
        auto id = static_cast<const flat::IdentifierType*>(method->maybe_request->type);

        // TODO(fxbug.dev/88343): switch on union/table when those are enabled.
        auto as_struct = static_cast<const flat::Struct*>(id->type_decl);
        if (!MaxBytesConstraint(reporter, attribute, as_struct)) {
          ok = false;
        }
      }
      if (method->maybe_response) {
        auto id = static_cast<const flat::IdentifierType*>(method->maybe_response->type);

        // TODO(fxbug.dev/88343): switch on union/table when those are enabled.
        auto as_struct = static_cast<const flat::Struct*>(id->type_decl);
        if (!MaxBytesConstraint(reporter, attribute, as_struct)) {
          ok = false;
        }
      }
      return ok;
    }
    case Element::Kind::kStruct: {
      auto struct_decl = static_cast<const Struct*>(element);
      max_bytes = struct_decl->typeshape(WireFormat::kV1NoEe).InlineSize() +
                  struct_decl->typeshape(WireFormat::kV1NoEe).MaxOutOfLine();
      break;
    }
    case Element::Kind::kTable: {
      auto table_decl = static_cast<const Table*>(element);
      max_bytes = table_decl->typeshape(WireFormat::kV1NoEe).InlineSize() +
                  table_decl->typeshape(WireFormat::kV1NoEe).MaxOutOfLine();
      break;
    }
    case Element::Kind::kUnion: {
      auto union_decl = static_cast<const Union*>(element);
      max_bytes = union_decl->typeshape(WireFormat::kV1NoEe).InlineSize() +
                  union_decl->typeshape(WireFormat::kV1NoEe).MaxOutOfLine();
      break;
    }
    default:
      assert(false && "unexpected kind");
      return false;
  }
  if (max_bytes > bound) {
    reporter->Fail(ErrTooManyBytes, attribute->span, bound, max_bytes);
    return false;
  }
  return true;
}

bool MaxHandlesConstraint(Reporter* reporter, const Attribute* attribute, const Element* element) {
  assert(element);
  auto arg = attribute->GetArg(AttributeArg::kDefaultAnonymousName);
  auto arg_value = static_cast<const flat::StringConstantValue&>(arg->value->Value());

  uint32_t bound;
  if (!ParseBound(reporter, attribute, std::string(arg_value.MakeContents()), &bound))
    return false;
  uint32_t max_handles = std::numeric_limits<uint32_t>::max();
  switch (element->kind) {
    case Element::Kind::kProtocol: {
      auto protocol = static_cast<const Protocol*>(element);
      bool ok = true;
      for (const auto& method_with_info : protocol->all_methods) {
        auto* method = method_with_info.method;
        if (!MaxHandlesConstraint(reporter, attribute, method)) {
          ok = false;
        }
      }
      return ok;
    }
    case Element::Kind::kProtocolMethod: {
      auto method = static_cast<const Protocol::Method*>(element);
      bool ok = true;
      if (method->maybe_request) {
        auto id = static_cast<const flat::IdentifierType*>(method->maybe_request->type);

        // TODO(fxbug.dev/88343): switch on union/table when those are enabled.
        auto as_struct = static_cast<const flat::Struct*>(id->type_decl);
        if (!MaxHandlesConstraint(reporter, attribute, as_struct)) {
          ok = false;
        }
      }
      if (method->maybe_response) {
        auto id = static_cast<const flat::IdentifierType*>(method->maybe_response->type);

        // TODO(fxbug.dev/88343): switch on union/table when those are enabled.
        auto as_struct = static_cast<const flat::Struct*>(id->type_decl);
        if (!MaxHandlesConstraint(reporter, attribute, as_struct)) {
          ok = false;
        }
      }
      return ok;
    }
    case Element::Kind::kStruct: {
      auto struct_decl = static_cast<const Struct*>(element);
      max_handles = struct_decl->typeshape(WireFormat::kV1NoEe).MaxHandles();
      break;
    }
    case Element::Kind::kTable: {
      auto table_decl = static_cast<const Table*>(element);
      max_handles = table_decl->typeshape(WireFormat::kV1NoEe).MaxHandles();
      break;
    }
    case Element::Kind::kUnion: {
      auto union_decl = static_cast<const Union*>(element);
      max_handles = union_decl->typeshape(WireFormat::kV1NoEe).MaxHandles();
      break;
    }
    default:
      assert(false && "unexpected kind");
      return false;
  }
  if (max_handles > bound) {
    reporter->Fail(ErrTooManyHandles, attribute->span, bound, max_handles);
    return false;
  }
  return true;
}

bool ResultShapeConstraint(Reporter* reporter, const Attribute* attribute, const Element* element) {
  assert(element);
  assert(element->kind == Element::Kind::kUnion);
  auto union_decl = static_cast<const Union*>(element);
  assert(union_decl->members.size() == 2);
  auto& error_member = union_decl->members.at(1);
  assert(error_member.maybe_used && "must have an error member");
  auto error_type = error_member.maybe_used->type_ctor->type;

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

  if (!error_primitive || (error_primitive->subtype != types::PrimitiveSubtype::kInt32 &&
                           error_primitive->subtype != types::PrimitiveSubtype::kUint32)) {
    reporter->Fail(ErrInvalidErrorType, union_decl->name.span().value());
    return false;
  }

  return true;
}

static std::string Trim(std::string s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
            return !utils::IsWhitespace(static_cast<char>(ch));
          }));
  s.erase(std::find_if(s.rbegin(), s.rend(),
                       [](int ch) { return !utils::IsWhitespace(static_cast<char>(ch)); })
              .base(),
          s.end());
  return s;
}

bool TransportConstraint(Reporter* reporter, const Attribute* attribute, const Element* element) {
  assert(element);
  assert(element->kind == Element::Kind::kProtocol);

  // function-local static pointer to non-trivially-destructible type
  // is allowed by styleguide
  static const auto kValidTransports = new std::set<std::string>{
      "Banjo",
      "Channel",
      "Driver",
      "Syscall",
  };

  auto arg = attribute->GetArg(AttributeArg::kDefaultAnonymousName);
  auto arg_value = static_cast<const flat::StringConstantValue&>(arg->value->Value());

  // Parse comma separated transports
  const std::string& value = arg_value.MakeContents();
  std::string::size_type prev_pos = 0;
  std::string::size_type pos;
  std::vector<std::string> transports;
  while ((pos = value.find(',', prev_pos)) != std::string::npos) {
    transports.emplace_back(Trim(value.substr(prev_pos, pos - prev_pos)));
    prev_pos = pos + 1;
  }
  transports.emplace_back(Trim(value.substr(prev_pos)));

  // Validate that they're ok
  for (const auto& transport : transports) {
    if (kValidTransports->count(transport) == 0) {
      reporter->Fail(ErrInvalidTransportType, attribute->span, transport, *kValidTransports);
      return false;
    }
  }
  return true;
}

Resource::Property* Resource::LookupProperty(std::string_view name) {
  for (Property& property : properties) {
    if (property.name.data() == name.data()) {
      return &property;
    }
  }
  return nullptr;
}

Libraries::Libraries() {
  AddAttributeSchema("discoverable")
      .RestrictTo({
          Element::Kind::kProtocol,
      });
  AddAttributeSchema(std::string(Attribute::kDocCommentName))
      .AddArg(AttributeArgSchema(ConstantValue::Kind::kString));
  AddAttributeSchema("layout").Deprecate();
  AddAttributeSchema("for_deprecated_c_bindings")
      .RestrictTo({
          Element::Kind::kProtocol,
          Element::Kind::kStruct,
      })
      .Constrain(SimpleLayoutConstraint);
  AddAttributeSchema("generated_name")
      .RestrictToAnonymousLayouts()
      .AddArg(AttributeArgSchema(ConstantValue::Kind::kString))
      .CompileEarly();
  AddAttributeSchema("max_bytes")
      .RestrictTo({
          Element::Kind::kProtocol,
          Element::Kind::kProtocolMethod,
          Element::Kind::kStruct,
          Element::Kind::kTable,
          Element::Kind::kUnion,
      })
      .AddArg(AttributeArgSchema(ConstantValue::Kind::kString))
      .Constrain(MaxBytesConstraint);
  AddAttributeSchema("max_handles")
      .RestrictTo({
          Element::Kind::kProtocol,
          Element::Kind::kProtocolMethod,
          Element::Kind::kStruct,
          Element::Kind::kTable,
          Element::Kind::kUnion,
      })
      .AddArg(AttributeArgSchema(ConstantValue::Kind::kString))
      .Constrain(MaxHandlesConstraint);
  AddAttributeSchema("result")
      .RestrictTo({
          Element::Kind::kUnion,
      })
      .Constrain(ResultShapeConstraint);
  AddAttributeSchema("selector")
      .RestrictTo({
          Element::Kind::kProtocolMethod,
      })
      .AddArg(AttributeArgSchema(ConstantValue::Kind::kString))
      .UseEarly();
  AddAttributeSchema("transitional")
      .RestrictTo({
          Element::Kind::kProtocolMethod,
      })
      .AddArg(AttributeArgSchema(ConstantValue::Kind::kString,
                                 AttributeArgSchema::Optionality::kOptional));
  AddAttributeSchema("transport")
      .RestrictTo({
          Element::Kind::kProtocol,
      })
      .AddArg(AttributeArgSchema(ConstantValue::Kind::kString))
      .Constrain(TransportConstraint);
  AddAttributeSchema("unknown").RestrictTo({
      Element::Kind::kEnumMember,
  });
}

Libraries::~Libraries() = default;
Libraries::Libraries(Libraries&&) noexcept = default;

bool Libraries::Insert(std::unique_ptr<Library> library) {
  auto [iter, inserted] = libraries_by_name_.try_emplace(library->name(), library.get());
  if (inserted) {
    libraries_.push_back(std::move(library));
  }
  return inserted;
}

Library* Libraries::Lookup(const std::vector<std::string_view>& library_name) const {
  auto iter = libraries_by_name_.find(library_name);
  return iter == libraries_by_name_.end() ? nullptr : iter->second;
}

AttributeSchema& Libraries::AddAttributeSchema(std::string name) {
  auto [it, inserted] = attribute_schemas_.try_emplace(std::move(name));
  assert(inserted && "do not add schemas twice");
  return it->second;
}

std::set<const Library*, LibraryComparator> Libraries::Unused() const {
  std::set<const Library*, LibraryComparator> unused;
  auto target = target_library();
  assert(target && "must have inserted at least one library");
  for (auto& library : libraries_) {
    if (library.get() != target) {
      unused.insert(library.get());
    }
  }
  std::set<const Library*> worklist = {target};
  while (!worklist.empty()) {
    auto it = worklist.begin();
    auto next = *it;
    worklist.erase(it);
    for (const auto dependency : next->dependencies()) {
      unused.erase(dependency);
      worklist.insert(dependency);
    }
  }
  return unused;
}

static size_t EditDistance(std::string_view sequence1, std::string_view sequence2) {
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
      this_row[i] = std::min(std::min(last_row[i] + 1, this_row[i - 1] + 1),
                             last_row[i - 1] + (s1c == s2c ? 0 : 1));
    }
    std::swap(last_row, this_row);
  }
  return last_row[s1_length];
}

const AttributeSchema& Libraries::RetrieveAttributeSchema(Reporter* reporter,
                                                          const Attribute* attribute,
                                                          bool warn_on_typo) const {
  auto attribute_name = attribute->name.data();
  auto iter = attribute_schemas_.find(attribute_name);
  if (iter != attribute_schemas_.end()) {
    return iter->second;
  }

  if (warn_on_typo) {
    // Match against all known attributes.
    for (const auto& [suspected_name, schema] : attribute_schemas_) {
      auto supplied_name = attribute_name;
      auto edit_distance = EditDistance(supplied_name, suspected_name);
      if (0 < edit_distance && edit_distance < 2) {
        reporter->Warn(WarnAttributeTypo, attribute->span, supplied_name, suspected_name);
      }
    }
  }
  return AttributeSchema::kUserDefined;
}

Dependencies::RegisterResult Dependencies::Register(
    const SourceSpan& span, std::string_view filename, Library* dep_library,
    const std::unique_ptr<raw::Identifier>& maybe_alias) {
  refs_.push_back(std::make_unique<LibraryRef>(span, dep_library));
  LibraryRef* ref = refs_.back().get();

  const std::vector<std::string_view> name =
      maybe_alias ? std::vector{maybe_alias->span().data()} : dep_library->name();
  auto iter = by_filename_.find(filename);
  if (iter == by_filename_.end()) {
    iter = by_filename_.emplace(filename, std::make_unique<PerFile>()).first;
  }
  PerFile& per_file = *iter->second;
  if (!per_file.libraries.insert(dep_library).second) {
    return RegisterResult::kDuplicate;
  }
  if (!per_file.refs.emplace(name, ref).second) {
    return RegisterResult::kCollision;
  }
  dependencies_aggregate_.insert(dep_library);
  return RegisterResult::kSuccess;
}

bool Dependencies::Contains(std::string_view filename, const std::vector<std::string_view>& name) {
  const auto iter = by_filename_.find(filename);
  if (iter == by_filename_.end()) {
    return false;
  }
  const PerFile& per_file = *iter->second;
  return per_file.refs.find(name) != per_file.refs.end();
}

Library* Dependencies::LookupAndMarkUsed(std::string_view filename,
                                         const std::vector<std::string_view>& name) const {
  auto iter1 = by_filename_.find(filename);
  if (iter1 == by_filename_.end()) {
    return nullptr;
  }

  auto iter2 = iter1->second->refs.find(name);
  if (iter2 == iter1->second->refs.end()) {
    return nullptr;
  }

  auto ref = iter2->second;
  ref->used = true;
  return ref->library;
}

void Dependencies::VerifyAllDependenciesWereUsed(const Library& for_library, Reporter* reporter) {
  for (const auto& [filename, per_file] : by_filename_) {
    for (const auto& [name, ref] : per_file->refs) {
      if (!ref->used) {
        reporter->Fail(ErrUnusedImport, ref->span, for_library.name(), ref->library->name(),
                       ref->library->name());
      }
    }
  }
}

std::string LibraryName(const Library* library, std::string_view separator) {
  if (library != nullptr) {
    return utils::StringJoin(library->name(), separator);
  }
  return std::string();
}

SourceSpan Library::GeneratedSimpleName(std::string_view name) {
  return generated_source_file_.AddLine(name);
}

bool Library::ConsumeFile(std::unique_ptr<raw::File> file) {
  return ConsumeStep(this, std::move(file)).Run();
}

bool Library::Compile() {
  if (!CompileStep(this).Run())
    return false;
  if (!SortStep(this).Run())
    return false;
  if (!VerifyResourcenessStep(this).Run())
    return false;
  if (!VerifyAttributesStep(this).Run())
    return false;
  if (!VerifyInlineSizeStep(this).Run())
    return false;
  if (!VerifyDependenciesStep(this).Run())
    return false;

  assert(reporter()->errors().empty() && "errors should have caused an early return");
  return true;
}

// Library resolution is concerned with resolving identifiers to their
// declarations, and with computing type sizes and alignments.

Decl* Library::LookupDeclByName(Name::Key name) const {
  auto iter = declarations_.find(name);
  if (iter == declarations_.end()) {
    return nullptr;
  }
  return iter->second;
}

bool HasSimpleLayout(const Decl* decl) {
  return decl->attributes->Get("for_deprecated_c_bindings") != nullptr;
}

const std::set<Library*>& Library::dependencies() const { return dependencies_.dependencies(); }

std::set<const Library*, LibraryComparator> Library::DirectAndComposedDependencies() const {
  std::set<const Library*, LibraryComparator> direct_dependencies;
  auto add_constant_deps = [&](const Constant* constant) {
    if (constant->kind != Constant::Kind::kIdentifier)
      return;
    auto* dep_library = static_cast<const IdentifierConstant*>(constant)->name.library();
    assert(dep_library != nullptr && "all identifier constants have a library");
    direct_dependencies.insert(dep_library);
  };
  auto add_type_ctor_deps = [&](const TypeConstructor& type_ctor) {
    if (auto dep_library = type_ctor.name.library())
      direct_dependencies.insert(dep_library);

    // TODO(fxbug.dev/64629): Add dependencies introduced through handle constraints.
    // This code currently assumes the handle constraints are always defined in the same
    // library as the resource_definition and so does not check for them separately.
    const auto& invocation = type_ctor.resolved_params;
    if (invocation.size_raw)
      add_constant_deps(invocation.size_raw);
    if (invocation.protocol_decl_raw)
      add_constant_deps(invocation.protocol_decl_raw);
    if (invocation.element_type_raw != nullptr) {
      if (auto dep_library = invocation.element_type_raw->name.library())
        direct_dependencies.insert(dep_library);
    }
    if (invocation.boxed_type_raw != nullptr) {
      if (auto dep_library = invocation.boxed_type_raw->name.library())
        direct_dependencies.insert(dep_library);
    }
  };
  for (const auto& dep_library : dependencies()) {
    direct_dependencies.insert(dep_library);
  }
  // Discover additional dependencies that are required to support
  // cross-library protocol composition.
  for (const auto& protocol : protocol_declarations_) {
    for (const auto method_with_info : protocol->all_methods) {
      if (method_with_info.method->maybe_request) {
        auto id =
            static_cast<const flat::IdentifierType*>(method_with_info.method->maybe_request->type);

        // TODO(fxbug.dev/88343): switch on union/table when those are enabled.
        auto as_struct = static_cast<const flat::Struct*>(id->type_decl);
        for (const auto& member : as_struct->members) {
          add_type_ctor_deps(*member.type_ctor);
        }
      }
      if (method_with_info.method->maybe_response) {
        auto id =
            static_cast<const flat::IdentifierType*>(method_with_info.method->maybe_response->type);

        // TODO(fxbug.dev/88343): switch on union/table when those are enabled.
        auto as_struct = static_cast<const flat::Struct*>(id->type_decl);
        for (const auto& member : as_struct->members) {
          add_type_ctor_deps(*member.type_ctor);
        }
      }
      direct_dependencies.insert(method_with_info.method->owning_protocol->name.library());
    }
  }
  direct_dependencies.erase(this);
  return direct_dependencies;
}

void Library::TraverseElements(const fit::function<void(Element*)>& fn) {
  fn(this);
  for (auto& [name, decl] : declarations_) {
    fn(decl);
    decl->ForEachMember(fn);
  }
}

void Decl::ForEachMember(const fit::function<void(Element*)>& fn) {
  switch (kind) {
    case Decl::Kind::kConst:
    case Decl::Kind::kTypeAlias:
      break;
    case Decl::Kind::kBits:
      for (auto& member : static_cast<Bits*>(this)->members) {
        fn(&member);
      }
      break;
    case Decl::Kind::kEnum:
      for (auto& member : static_cast<Enum*>(this)->members) {
        fn(&member);
      }
      break;
    case Decl::Kind::kProtocol:
      for (auto& composed_protocol : static_cast<Protocol*>(this)->composed_protocols) {
        fn(&composed_protocol);
      }
      for (auto& method : static_cast<Protocol*>(this)->methods) {
        fn(&method);
      }
      break;
    case Decl::Kind::kResource:
      for (auto& member : static_cast<Resource*>(this)->properties) {
        fn(&member);
      }
      break;
    case Decl::Kind::kService:
      for (auto& member : static_cast<Service*>(this)->members) {
        fn(&member);
      }
      break;
    case Decl::Kind::kStruct:
      for (auto& member : static_cast<Struct*>(this)->members) {
        fn(&member);
      }
      break;
    case Decl::Kind::kTable:
      for (auto& member : static_cast<Table*>(this)->members) {
        fn(&member);
      }
      break;
    case Decl::Kind::kUnion:
      for (auto& member : static_cast<Union*>(this)->members) {
        fn(&member);
      }
      break;
  }  // switch
}

std::unique_ptr<TypeConstructor> TypeConstructor::CreateSizeType() {
  std::vector<std::unique_ptr<LayoutParameter>> no_params;
  std::vector<std::unique_ptr<Constant>> no_constraints;
  return std::make_unique<TypeConstructor>(
      Name::CreateIntrinsic("uint32"),
      std::make_unique<LayoutParameterList>(std::move(no_params), std::nullopt /* span */),
      std::make_unique<TypeConstraints>(std::move(no_constraints), std::nullopt /* span */));
}

bool LibraryMediator::ResolveParamAsType(const flat::TypeTemplate* layout,
                                         const std::unique_ptr<LayoutParameter>& param,
                                         const Type** out_type) const {
  auto type_ctor = param->AsTypeCtor();
  auto check = reporter()->Checkpoint();
  if (!type_ctor || !ResolveType(type_ctor)) {
    // if there were no errors reported but we couldn't resolve to a type, it must
    // mean that the parameter referred to a non-type, so report a new error here.
    if (check.NoNewErrors()) {
      return Fail(ErrExpectedType, param->span);
    }
    // otherwise, there was an error during the type resolution process, so we
    // should just report that rather than add an extra error here
    return false;
  }
  *out_type = type_ctor->type;
  return true;
}

bool LibraryMediator::ResolveParamAsSize(const flat::TypeTemplate* layout,
                                         const std::unique_ptr<LayoutParameter>& param,
                                         const Size** out_size) const {
  // We could use param->AsConstant() here, leading to code similar to ResolveParamAsType.
  // However, unlike ErrExpectedType, ErrExpectedValueButGotType requires a name to be
  // reported, which would require doing a switch on the parameter kind anyway to find
  // its Name. So we just handle all the cases ourselves from the start.
  switch (param->kind) {
    case LayoutParameter::Kind::kLiteral: {
      auto literal_param = static_cast<LiteralLayoutParameter*>(param.get());
      if (!ResolveSizeBound(literal_param->literal.get(), out_size))
        return Fail(ErrCouldNotParseSizeBound, literal_param->span);
      break;
    }
    case LayoutParameter::kType: {
      auto type_param = static_cast<TypeLayoutParameter*>(param.get());
      return Fail(ErrExpectedValueButGotType, type_param->span, type_param->type_ctor->name);
    }
    case LayoutParameter::Kind::kIdentifier: {
      auto ambig_param = static_cast<IdentifierLayoutParameter*>(param.get());
      auto as_constant = ambig_param->AsConstant();
      if (!ResolveSizeBound(as_constant, out_size))
        return Fail(ErrExpectedValueButGotType, ambig_param->span, ambig_param->name);
      break;
    }
  }
  assert(*out_size);
  if ((*out_size)->value == 0)
    return Fail(ErrMustHaveNonZeroSize, param->span, layout);
  return true;
}

bool LibraryMediator::ResolveConstraintAs(Constant* constraint,
                                          const std::vector<ConstraintKind>& interpretations,
                                          Resource* resource, ResolvedConstraint* out) const {
  for (const auto& constraint_kind : interpretations) {
    out->kind = constraint_kind;
    switch (constraint_kind) {
      case ConstraintKind::kHandleSubtype: {
        assert(resource &&
               "Compiler bug: must pass resource if trying to resolve to handle subtype");
        if (ResolveAsHandleSubtype(resource, constraint, &out->value.handle_subtype))
          return true;
        break;
      }
      case ConstraintKind::kHandleRights: {
        assert(resource &&
               "Compiler bug: must pass resource if trying to resolve to handle rights");
        if (ResolveAsHandleRights(resource, constraint, &(out->value.handle_rights)))
          return true;
        break;
      }
      case ConstraintKind::kSize: {
        if (ResolveSizeBound(constraint, &(out->value.size)))
          return true;
        break;
      }
      case ConstraintKind::kNullability: {
        if (ResolveAsOptional(constraint))
          return true;
        break;
      }
      case ConstraintKind::kProtocol: {
        if (ResolveAsProtocol(constraint, &(out->value.protocol_decl)))
          return true;
        break;
      }
    }
  }
  return false;
}

bool LibraryMediator::ResolveType(TypeConstructor* type) const {
  compile_step_->CompileTypeConstructor(type);
  return type->type != nullptr;
}

bool LibraryMediator::ResolveSizeBound(Constant* size_constant, const Size** out_size) const {
  return compile_step_->ResolveSizeBound(size_constant, out_size);
}

bool LibraryMediator::ResolveAsOptional(Constant* constant) const {
  return compile_step_->ResolveAsOptional(constant);
}

bool LibraryMediator::ResolveAsHandleSubtype(Resource* resource, Constant* constant,
                                             uint32_t* out_obj_type) const {
  return compile_step_->ResolveHandleSubtypeIdentifier(resource, constant, out_obj_type);
}

bool LibraryMediator::ResolveAsHandleRights(Resource* resource, Constant* constant,
                                            const HandleRights** out_rights) const {
  return compile_step_->ResolveHandleRightsConstant(resource, constant, out_rights);
}

bool LibraryMediator::ResolveAsProtocol(const Constant* constant, const Protocol** out_decl) const {
  // TODO(fxbug.dev/75112): If/when this method is responsible for reporting errors, the
  // `return false` statements should fail with ErrConstraintMustBeProtocol instead.
  if (constant->kind != Constant::Kind::kIdentifier)
    return false;

  const auto* as_identifier = static_cast<const IdentifierConstant*>(constant);
  const auto* decl = LookupDeclByName(as_identifier->name);
  if (!decl || decl->kind != Decl::Kind::kProtocol)
    return false;
  *out_decl = static_cast<const Protocol*>(decl);
  return true;
}

Decl* LibraryMediator::LookupDeclByName(Name::Key name) const {
  return library_->LookupDeclByName(name);
}

TypeConstructor* LiteralLayoutParameter::AsTypeCtor() const { return nullptr; }
TypeConstructor* TypeLayoutParameter::AsTypeCtor() const { return type_ctor.get(); }
TypeConstructor* IdentifierLayoutParameter::AsTypeCtor() const {
  if (!as_type_ctor) {
    std::vector<std::unique_ptr<LayoutParameter>> no_params;
    std::vector<std::unique_ptr<Constant>> no_constraints;
    as_type_ctor = std::make_unique<TypeConstructor>(
        name, std::make_unique<LayoutParameterList>(std::move(no_params), std::nullopt),
        std::make_unique<TypeConstraints>(std::move(no_constraints), std::nullopt));
  }

  return as_type_ctor.get();
}

Constant* LiteralLayoutParameter::AsConstant() const { return literal.get(); }
Constant* TypeLayoutParameter::AsConstant() const { return nullptr; }
Constant* IdentifierLayoutParameter::AsConstant() const {
  if (!as_constant) {
    as_constant = std::make_unique<IdentifierConstant>(name, span);
  }
  return as_constant.get();
}

void LibraryMediator::CompileDecl(Decl* decl) const { compile_step_->CompileDecl(decl); }
std::optional<std::vector<const Decl*>> LibraryMediator::GetDeclCycle(const Decl* decl) const {
  return compile_step_->GetDeclCycle(decl);
}

std::any Bits::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Enum::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Protocol::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Service::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Struct::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Struct::Member::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Table::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Table::Member::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Table::Member::Used::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Union::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Union::Member::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any Union::Member::Used::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }

}  // namespace fidl::flat
