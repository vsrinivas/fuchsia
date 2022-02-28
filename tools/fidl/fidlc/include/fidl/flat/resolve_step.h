// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_RESOLVE_STEP_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_RESOLVE_STEP_H_

#include "fidl/flat/compiler.h"

namespace fidl::flat {

// This step resolves all references in the library, linking them to the element
// that their name refers to. It does not resolve constant values (i.e. calling
// Constant::ResolveTo); that happens in the CompileStep.
class ResolveStep : public Compiler::Step {
 public:
  using Step::Step;

 private:
  void RunImpl() override;

  // Called early to resolve references that are needed to provide context when
  // resolving other references.
  void ResolveForContext();

  // Context allows certain lookups to be contextual, i.e. the meaning of the
  // name depends on where it occurs in the AST.
  struct Context {
    static const Context kNone;
    // Allows naming handle subtypes unqualified, e.g. in `zx.handle:CHANNEL`,
    // the context for `CHANNEL` is the `zx.obj_type` enum.
    Enum* maybe_resource_subtype = nullptr;
  };

  // Returns the context to use when resolving type_ctor's constraints.
  static Context ConstraintContext(const TypeConstructor* type_ctor);

  void ResolveElement(Element* element);
  void ResolveTypeConstructor(TypeConstructor* type_ctor);
  void ResolveConstant(Constant* constant, Context context = Context::kNone);
  void ResolveReference(Reference& ref, Context context = Context::kNone);
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_RESOLVE_STEP_H_
