// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat/compiler.h"

#include "fidl/flat/attribute_schema.h"
#include "fidl/flat/compile_step.h"
#include "fidl/flat/consume_step.h"
#include "fidl/flat/resolve_step.h"
#include "fidl/flat/sort_step.h"
#include "fidl/flat/verify_steps.h"

namespace fidl::flat {

Compiler::Compiler(Libraries* all_libraries, ordinals::MethodHasher method_hasher,
                   ExperimentalFlags experimental_flags)
    : ReporterMixin(all_libraries->reporter()),
      library_(std::make_unique<Library>()),
      all_libraries_(all_libraries),
      method_hasher_(std::move(method_hasher)),
      experimental_flags_(experimental_flags) {}

bool Compiler::ConsumeFile(std::unique_ptr<raw::File> file) {
  return ConsumeStep(this, std::move(file)).Run();
}

std::unique_ptr<Library> Compiler::Compile() {
  [[maybe_unused]] auto checkpoint = reporter()->Checkpoint();

  if (!ResolveStep(this).Run())
    return nullptr;
  if (!CompileStep(this).Run())
    return nullptr;
  if (!SortStep(this).Run())
    return nullptr;
  if (!VerifyResourcenessStep(this).Run())
    return nullptr;
  if (!VerifyAttributesStep(this).Run())
    return nullptr;
  if (!VerifyInlineSizeStep(this).Run())
    return nullptr;
  if (!VerifyDependenciesStep(this).Run())
    return nullptr;
  if (experimental_flags_.IsFlagEnabled(ExperimentalFlags::Flag::kUnknownInteractions)) {
    if (!VerifyOpenInteractionsStep(this).Run())
      return nullptr;
  }

  assert(checkpoint.NoNewErrors() && "errors should have caused an early return");
  return std::move(library_);
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
  auto [iter, inserted] = libraries_by_name_.try_emplace(library->name, library.get());
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
  [[maybe_unused]] auto num_removed = libraries_by_name_.erase(library->name);
  assert(num_removed == 1 && "library not in libraries_by_name_");
  auto iter = std::find_if(libraries_.begin(), libraries_.end(),
                           [&](auto& lib) { return lib.get() == library; });
  assert(iter != libraries_.end() && "library not in libraries_");
  libraries_.erase(iter);
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
    for (const auto dependency : next->dependencies.all()) {
      unused.erase(dependency);
      worklist.insert(dependency);
    }
  }
  return unused;
}

std::vector<const Decl*> Libraries::DeclarationOrder() const {
  std::vector<const Decl*> order;
  for (auto& library : libraries_) {
    order.insert(order.end(), library->declaration_order.begin(), library->declaration_order.end());
  };
  return order;
}

std::set<const Library*, LibraryComparator> Libraries::DirectAndComposedDependencies(
    const Library* library) const {
  std::set<const Library*, LibraryComparator> direct_dependencies;
  auto add_constant_deps = [&](const Constant* constant) {
    if (constant->kind != Constant::Kind::kIdentifier)
      return;
    auto identifier_constant = static_cast<const IdentifierConstant*>(constant);
    if (auto dep_library = identifier_constant->reference.target_library())
      direct_dependencies.insert(dep_library);
  };
  auto add_type_ctor_deps = [&](const TypeConstructor& type_ctor) {
    if (auto dep_library = type_ctor.layout.target_library())
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
      if (auto dep_library = invocation.element_type_raw->layout.target_library())
        direct_dependencies.insert(dep_library);
    }
    if (invocation.boxed_type_raw != nullptr) {
      if (auto dep_library = invocation.boxed_type_raw->layout.target_library())
        direct_dependencies.insert(dep_library);
    }
  };
  for (const auto& dep_library : library->dependencies.all()) {
    direct_dependencies.insert(dep_library);
  }
  // Discover additional dependencies that are required to support
  // cross-library protocol composition.
  for (const auto& protocol : library->protocol_declarations) {
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
  direct_dependencies.erase(library);
  direct_dependencies.erase(root_library());
  return direct_dependencies;
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

const AttributeSchema& Libraries::RetrieveAttributeSchema(const Attribute* attribute,
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
        Warn(WarnAttributeTypo, attribute->span, supplied_name, suspected_name);
      }
    }
  }
  return AttributeSchema::kUserDefined;
}

}  // namespace fidl::flat
