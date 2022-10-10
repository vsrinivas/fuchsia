// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/flat/compiler.h"

#include <zircon/assert.h>

#include "tools/fidl/fidlc/include/fidl/flat/attribute_schema.h"
#include "tools/fidl/fidlc/include/fidl/flat/availability_step.h"
#include "tools/fidl/fidlc/include/fidl/flat/compile_step.h"
#include "tools/fidl/fidlc/include/fidl/flat/consume_step.h"
#include "tools/fidl/fidlc/include/fidl/flat/resolve_step.h"
#include "tools/fidl/fidlc/include/fidl/flat/sort_step.h"
#include "tools/fidl/fidlc/include/fidl/flat/verify_steps.h"
#include "tools/fidl/fidlc/include/fidl/names.h"

namespace fidl::flat {

Compiler::Compiler(Libraries* all_libraries, const VersionSelection* version_selection,
                   ordinals::MethodHasher method_hasher, ExperimentalFlags experimental_flags)
    : ReporterMixin(all_libraries->reporter()),
      library_(std::make_unique<Library>()),
      all_libraries_(all_libraries),
      version_selection(version_selection),
      method_hasher_(std::move(method_hasher)),
      experimental_flags_(experimental_flags) {}

bool Compiler::ConsumeFile(std::unique_ptr<raw::File> file) {
  return ConsumeStep(this, std::move(file)).Run();
}

bool Compiler::Compile() {
  auto checkpoint = reporter()->Checkpoint();

  if (!AvailabilityStep(this).Run())
    return false;
  if (!ResolveStep(this).Run())
    return false;
  if (!CompileStep(this).Run())
    return false;
  if (!SortStep(this).Run())
    return false;
  if (!VerifyResourcenessStep(this).Run())
    return false;
  if (!VerifyHandleTransportCompatibilityStep(this).Run())
    return false;
  if (!VerifyAttributesStep(this).Run())
    return false;
  if (!VerifyInlineSizeStep(this).Run())
    return false;
  if (!VerifyDependenciesStep(this).Run())
    return false;
  if (experimental_flags_.IsFlagEnabled(ExperimentalFlags::Flag::kUnknownInteractions)) {
    if (!VerifyOpenInteractionsStep(this).Run())
      return false;
  }

  if (!all_libraries_->Insert(std::move(library_)))
    return false;

  ZX_ASSERT_MSG(checkpoint.NoNewErrors(), "errors should have caused an early return");
  return true;
}

bool Compiler::Step::Run() {
  auto checkpoint = reporter()->Checkpoint();
  RunImpl();
  return checkpoint.NoNewErrors();
}

Typespace* Compiler::Step::typespace() { return compiler_->all_libraries_->typespace(); }

VirtualSourceFile* Compiler::Step::generated_source_file() {
  return compiler_->all_libraries_->generated_source_file();
}

bool Libraries::Insert(std::unique_ptr<Library> library) {
  auto [_, inserted] = libraries_by_name_.try_emplace(library->name, library.get());
  if (!inserted) {
    return Fail(ErrMultipleLibrariesWithSameName, library->arbitrary_name_span, library->name);
  }
  libraries_.push_back(std::move(library));
  return true;
}

Library* Libraries::Lookup(const std::vector<std::string_view>& library_name) const {
  auto iter = libraries_by_name_.find(library_name);
  return iter == libraries_by_name_.end() ? nullptr : iter->second;
}

void Libraries::Remove(const Library* library) {
  auto num_removed = libraries_by_name_.erase(library->name);
  ZX_ASSERT_MSG(num_removed == 1, "library not in libraries_by_name_");
  auto iter = std::find_if(libraries_.begin(), libraries_.end(),
                           [&](auto& lib) { return lib.get() == library; });
  ZX_ASSERT_MSG(iter != libraries_.end(), "library not in libraries_");
  libraries_.erase(iter);
}

AttributeSchema& Libraries::AddAttributeSchema(std::string name) {
  auto [it, inserted] = attribute_schemas_.try_emplace(std::move(name));
  ZX_ASSERT_MSG(inserted, "do not add schemas twice");
  return it->second;
}

std::set<const Library*, LibraryComparator> Libraries::Unused() const {
  std::set<const Library*, LibraryComparator> unused;
  auto target = libraries_.back().get();
  ZX_ASSERT_MSG(target, "must have inserted at least one library");
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
    for (const auto dependency : next->dependencies.all()) {
      unused.erase(dependency);
      worklist.insert(dependency);
    }
  }
  return unused;
}

std::set<Platform, Platform::Compare> Libraries::Platforms() const {
  std::set<Platform, Platform::Compare> platforms;
  for (auto& library : libraries_) {
    platforms.insert(library->platform.value());
  }
  return platforms;
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

const AttributeSchema& Libraries::RetrieveAttributeSchema(const Attribute* attribute) const {
  auto attribute_name = attribute->name.data();
  auto iter = attribute_schemas_.find(attribute_name);
  if (iter != attribute_schemas_.end()) {
    return iter->second;
  }
  return AttributeSchema::kUserDefined;
}

void Libraries::WarnOnAttributeTypo(const Attribute* attribute) const {
  auto attribute_name = attribute->name.data();
  auto iter = attribute_schemas_.find(attribute_name);
  if (iter != attribute_schemas_.end()) {
    return;
  }
  for (const auto& [suspected_name, schema] : attribute_schemas_) {
    auto supplied_name = attribute_name;
    auto edit_distance = EditDistance(supplied_name, suspected_name);
    if (0 < edit_distance && edit_distance < 2) {
      Warn(WarnAttributeTypo, attribute->span, supplied_name, suspected_name);
    }
  }
}

// Helper function to calculate Compilation::external_structs.
static std::vector<const flat::Struct*> ExternalStructs(
    const Library* target_library, const std::vector<const Protocol*>& protocols) {
  // Use the comparator below to ensure deterministic output when this set is
  // converted into a vector at the end of this function.
  auto ordering = [](const flat::Struct* a, const flat::Struct* b) {
    return NameFlatName(a->name) < NameFlatName(b->name);
  };
  std::set<const flat::Struct*, decltype(ordering)> external_structs(ordering);

  for (const auto& protocol : protocols) {
    for (const auto method_with_info : protocol->all_methods) {
      const auto& method = method_with_info.method;
      if (method->maybe_request) {
        auto id = static_cast<const flat::IdentifierType*>(method->maybe_request->type);

        // Make sure this is actually an externally defined struct before proceeding.
        if (id->name.library() != target_library &&
            id->type_decl->kind == flat::Decl::Kind::kStruct) {
          auto as_struct = static_cast<const flat::Struct*>(id->type_decl);
          external_structs.insert(as_struct);
        }
      }
      if (method->maybe_response) {
        auto id = static_cast<const flat::IdentifierType*>(method->maybe_response->type);

        // Make sure this is actually an externally defined struct before proceeding.
        if (id->name.library() != target_library &&
            id->type_decl->kind == flat::Decl::Kind::kStruct) {
          auto as_struct = static_cast<const flat::Struct*>(id->type_decl);
          external_structs.insert(as_struct);
        }

        // This struct is actually wrapping an error union, so check to see if the success variant
        // struct should be exported as well.
        if (method->has_error) {
          auto response_struct = static_cast<const flat::Struct*>(id->type_decl);
          const auto* result_union_type =
              static_cast<const flat::IdentifierType*>(response_struct->members[0].type_ctor->type);

          ZX_ASSERT(result_union_type->type_decl->kind == flat::Decl::Kind::kUnion);
          const auto* result_union = static_cast<const flat::Union*>(result_union_type->type_decl);
          const auto* success_variant_type = static_cast<const flat::IdentifierType*>(
              result_union->members[0].maybe_used->type_ctor->type);
          if (success_variant_type->type_decl->kind != flat::Decl::Kind::kStruct) {
            continue;
          }
          const auto* success_variant_struct =
              static_cast<const flat::Struct*>(success_variant_type->type_decl);

          // Make sure this is actually an externally defined struct before proceeding.
          if (success_variant_type->name.library() != target_library) {
            external_structs.insert(success_variant_struct);
          }
        }
      }
    }
  }

  return std::vector<const flat::Struct*>(external_structs.begin(), external_structs.end());
}

namespace {

// Helper class to calculate Compilation::direct_and_composed_dependencies.
class CalcDepedencies {
 public:
  std::set<const Library*, LibraryComparator> From(std::vector<const Decl*>& roots) && {
    for (const Decl* decl : roots) {
      VisitDecl(decl);
    }
    return std::move(deps_);
  }

 private:
  std::set<const Library*, LibraryComparator> deps_;

  void VisitDecl(const Decl* decl) {
    switch (decl->kind) {
      case Decl::Kind::kBuiltin: {
        ZX_PANIC("unexpected builtin");
      }
      case Decl::Kind::kBits: {
        auto bits_decl = static_cast<const Bits*>(decl);
        VisitTypeConstructor(bits_decl->subtype_ctor.get());
        for (auto& member : bits_decl->members) {
          VisitConstant(member.value.get());
        }
        break;
      }
      case Decl::Kind::kConst: {
        auto const_decl = static_cast<const Const*>(decl);
        VisitTypeConstructor(const_decl->type_ctor.get());
        VisitConstant(const_decl->value.get());
        break;
      }
      case Decl::Kind::kEnum: {
        auto enum_decl = static_cast<const Enum*>(decl);
        VisitTypeConstructor(enum_decl->subtype_ctor.get());
        for (auto& member : enum_decl->members) {
          VisitConstant(member.value.get());
        }
        break;
      }
      case Decl::Kind::kProtocol: {
        auto protocol_decl = static_cast<const Protocol*>(decl);
        // Make sure we insert libraries for composed protocols, even if those
        // protocols are empty (so we don't get the dependency from a method).
        for (auto& composed_protocol : protocol_decl->composed_protocols) {
          VisitReference(composed_protocol.reference);
        }
        for (auto& method_with_info : protocol_decl->all_methods) {
          // Make sure we insert libraries for all transitive composed
          // protocols, even if they have no methods with payloads.
          deps_.insert(method_with_info.method->owning_protocol->name.library());
          for (auto type_ctor : {method_with_info.method->maybe_request.get(),
                                 method_with_info.method->maybe_response.get()}) {
            if (type_ctor) {
              VisitTypeConstructor(type_ctor);
              auto type_decl = static_cast<const flat::IdentifierType*>(type_ctor->type)->type_decl;
              // Since we flatten struct parameters, we need to add dependencies
              // as if they were copied and pasted into the library.
              if (type_decl->kind == Decl::Kind::kStruct) {
                VisitDecl(static_cast<const Struct*>(type_decl));
              }
            }
          }
        }
        break;
      }
      case Decl::Kind::kResource: {
        auto resource_decl = static_cast<const Resource*>(decl);
        VisitTypeConstructor(resource_decl->subtype_ctor.get());
        for (auto& property : resource_decl->properties) {
          VisitTypeConstructor(property.type_ctor.get());
        }
        break;
      }
      case Decl::Kind::kService: {
        auto service_decl = static_cast<const Service*>(decl);
        for (auto& member : service_decl->members) {
          VisitTypeConstructor(member.type_ctor.get());
        }
        break;
      }
      case Decl::Kind::kStruct: {
        auto struct_decl = static_cast<const Struct*>(decl);
        for (auto& member : struct_decl->members) {
          VisitTypeConstructor(member.type_ctor.get());
          if (auto& value = member.maybe_default_value) {
            VisitConstant(value.get());
          }
        }
        break;
      }
      case Decl::Kind::kTable: {
        auto table_decl = static_cast<const Table*>(decl);
        for (auto& member : table_decl->members) {
          if (auto& used = member.maybe_used) {
            VisitTypeConstructor(used->type_ctor.get());
          }
        }
        break;
      }
      case Decl::Kind::kAlias: {
        auto alias_decl = static_cast<const Alias*>(decl);
        VisitTypeConstructor(alias_decl->partial_type_ctor.get());
        break;
      }
      case Decl::Kind::kNewType: {
        auto new_type_decl = static_cast<const NewType*>(decl);
        VisitTypeConstructor(new_type_decl->type_ctor.get());
        break;
      }
      case Decl::Kind::kUnion: {
        auto union_decl = static_cast<const Union*>(decl);
        for (auto& member : union_decl->members) {
          if (auto& used = member.maybe_used) {
            VisitTypeConstructor(used->type_ctor.get());
          }
        }
        break;
      }
    }
  }

  void VisitTypeConstructor(const TypeConstructor* type_ctor) {
    VisitReference(type_ctor->layout);

    // TODO(fxbug.dev/64629): Add dependencies introduced through handle constraints.
    // This code currently assumes the handle constraints are always defined in the same
    // library as the resource_definition and so does not check for them separately.
    const auto& invocation = type_ctor->resolved_params;
    if (invocation.size_raw) {
      VisitConstant(invocation.size_raw);
    }
    if (invocation.protocol_decl_raw) {
      VisitConstant(invocation.protocol_decl_raw);
    }
    if (invocation.element_type_raw) {
      VisitReference(invocation.element_type_raw->layout);
    }
    if (invocation.boxed_type_raw) {
      VisitReference(invocation.boxed_type_raw->layout);
    }
  }

  void VisitConstant(const Constant* constant) {
    switch (constant->kind) {
      case Constant::Kind::kLiteral:
        break;
      case Constant::Kind::kIdentifier: {
        auto identifier_constant = static_cast<const IdentifierConstant*>(constant);
        VisitReference(identifier_constant->reference);
        break;
      }
      case Constant::Kind::kBinaryOperator: {
        auto binop_constant = static_cast<const BinaryOperatorConstant*>(constant);
        VisitConstant(binop_constant->left_operand.get());
        VisitConstant(binop_constant->right_operand.get());
        break;
      }
    }
  }

  void VisitReference(const Reference& reference) { deps_.insert(reference.resolved().library()); }
};

}  // namespace

std::unique_ptr<Compilation> Libraries::Filter(const VersionSelection* version_selection) {
  // Returns true if decl should be included based on the version selection.
  auto keep = [&](const auto& decl) {
    return decl->availability.range().Contains(
        version_selection->Lookup(decl->name.library()->platform.value()));
  };

  // Copies decl pointers for which keep() returns true from src to dst.
  auto filter = [&](auto* dst, const auto& src) {
    for (const auto& decl : src) {
      if (keep(decl)) {
        dst->push_back(&*decl);
      }
    }
  };

  // Filters a Library::Declarations into a Compilation::Declarations.
  auto filter_declarations = [&](Compilation::Declarations* dst, const Library::Declarations& src) {
    filter(&dst->bits, src.bits);
    filter(&dst->builtins, src.builtins);
    filter(&dst->consts, src.consts);
    filter(&dst->enums, src.enums);
    filter(&dst->new_types, src.new_types);
    filter(&dst->protocols, src.protocols);
    filter(&dst->resources, src.resources);
    filter(&dst->services, src.services);
    filter(&dst->structs, src.structs);
    filter(&dst->tables, src.tables);
    filter(&dst->aliases, src.aliases);
    filter(&dst->unions, src.unions);
  };

  ZX_ASSERT(!libraries_.empty());
  auto library = libraries_.back().get();
  auto compilation = std::make_unique<Compilation>();
  compilation->library_name = library->name;
  compilation->library_declarations = library->library_name_declarations;
  compilation->library_attributes = library->attributes.get();
  filter_declarations(&compilation->declarations, library->declarations);
  compilation->external_structs = ExternalStructs(library, compilation->declarations.protocols);
  compilation->using_references = library->dependencies.library_references();
  filter(&compilation->declaration_order, library->declaration_order);
  for (const auto& lib : libraries_) {
    filter(&compilation->all_libraries_declaration_order, lib->declaration_order);
  };
  auto dependencies = CalcDepedencies().From(compilation->declaration_order);
  dependencies.erase(library);
  dependencies.erase(root_library());
  for (auto dep_library : dependencies) {
    auto& dep = compilation->direct_and_composed_dependencies.emplace_back();
    dep.library = dep_library;
    filter_declarations(&dep.declarations, dep_library->declarations);
  }

  return compilation;
}

}  // namespace fidl::flat
