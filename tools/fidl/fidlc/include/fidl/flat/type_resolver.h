// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_TYPE_RESOLVER_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_TYPE_RESOLVER_H_

#include "tools/fidl/fidlc/include/fidl/flat/compile_step.h"
#include "tools/fidl/fidlc/include/fidl/flat_ast.h"

namespace fidl::flat {

class CompileStep;

// TypeResolver exposes Resolve* methods from CompileStep to Typespace and Type.
class TypeResolver : private ReporterMixin {
 public:
  explicit TypeResolver(CompileStep* compile_step)
      : ReporterMixin(compile_step->reporter()), compile_step_(compile_step) {}

  using ReporterMixin::Fail;

  const ExperimentalFlags& experimental_flags() const {
    return compile_step_->experimental_flags();
  }

  // Top level methods for resolving layout parameters. These are used by
  // TypeTemplates.
  bool ResolveParamAsType(const Reference& layout, const std::unique_ptr<LayoutParameter>& param,
                          const Type** out_type);
  bool ResolveParamAsSize(const Reference& layout, const std::unique_ptr<LayoutParameter>& param,
                          const Size** out_size);

  // Top level methods for resolving constraints. These are used by Types
  enum class ConstraintKind {
    kHandleSubtype,
    kHandleRights,
    kSize,
    kNullability,
    kProtocol,
  };

  struct ResolvedConstraint {
    ConstraintKind kind;

    union Value {
      uint32_t handle_subtype;
      const HandleRights* handle_rights;
      const Size* size;
      // Storing a value for nullability is redundant, since there's only one possible value - if we
      // resolved to optional, then the caller knows that the resulting value is
      // types::Nullability::kNullable.
      const Protocol* protocol_decl;
    } value;
  };

  // Convenience method to iterate through the possible interpretations, returning the first one
  // that succeeds. This is valid because the interpretations are mutually exclusive, since a Name
  // can only ever refer to one kind of thing.
  bool ResolveConstraintAs(Constant* constraint, const std::vector<ConstraintKind>& interpretations,
                           Resource* resource_decl, ResolvedConstraint* out);

  // These methods forward their implementation to the library_. They are used
  // by the top level methods above
  bool ResolveType(TypeConstructor* type);
  bool ResolveSizeBound(Constant* size_constant, const Size** out_size);
  bool ResolveAsOptional(Constant* constant);
  bool ResolveAsHandleSubtype(Resource* resource, Constant* constant, uint32_t* out_obj_type);
  bool ResolveAsHandleRights(Resource* resource, Constant* constant,
                             const HandleRights** out_rights);
  bool ResolveAsProtocol(const Constant* size_constant, const Protocol** out_decl);

  // Used in Typespace::Creator::Create{Identifier,Alias}Type to recursively
  // compile the right-hand side.
  void CompileDecl(Decl* decl);

  // Use in Typespace::Creator::CreateAliasType to check for cycles.
  std::optional<std::vector<const Decl*>> GetDeclCycle(const Decl* decl);

 private:
  CompileStep* compile_step_;
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_TYPE_RESOLVER_H_
