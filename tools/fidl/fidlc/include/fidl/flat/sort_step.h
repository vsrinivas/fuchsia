// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_SORT_STEP_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_SORT_STEP_H_

#include "fidl/flat/compiler.h"

namespace fidl::flat {

// SortStep topologically sorts the library's decls, or fails if it detects a
// cycle. It stores the result in library_->declaration_order_. See also
// Libraries::DeclarationOrder() which includes all transitive dependencies.
//
// TODO(fxbug.dev/7660): This is only used by C/C++ backends. We should remove
// it and the JSON IR field "declaration_order", preferring to calculate this in
// fidlgenlib whe needed. We would still have to detect cycles, but this can be
// done in CompileStep recursion, e.g. compiling the TypeConstructor layout
// if it does not have the "optional" constraint (currently it is never done).
class SortStep : public Compiler::Step {
 public:
  using Step::Step;

 private:
  void RunImpl() override;
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_SORT_STEP_H_
