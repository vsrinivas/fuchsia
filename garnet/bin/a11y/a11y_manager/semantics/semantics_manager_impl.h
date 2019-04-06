// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_A11Y_MANAGER_SEMANTICS_SEMANTICS_MANAGER_IMPL_H_
#define GARNET_BIN_A11Y_A11Y_MANAGER_SEMANTICS_SEMANTICS_MANAGER_IMPL_H_
#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/vfs/cpp/pseudo_file.h>

#include "garnet/bin/a11y/a11y_manager/semantics/semantic_tree_impl.h"

namespace a11y_manager {
class SemanticsManagerImpl
    : public fuchsia::accessibility::semantics::SemanticsManager {
 public:
  explicit SemanticsManagerImpl() = default;
  ~SemanticsManagerImpl() = default;

  void AddBinding(fidl::InterfaceRequest<
                  fuchsia::accessibility::semantics::SemanticsManager>
                      request);

  void SetDebugDirectory(vfs::PseudoDir* debug_dir);

 private:
  // |fuchsia::accessibility::semantics::SemanticsManager|:
  void RegisterView(
      zx::event view_ref,
      fidl::InterfaceHandle<
          fuchsia::accessibility::semantics::SemanticActionListener>
          handle,
      fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree>
          semantic_tree) override;

  fidl::BindingSet<fuchsia::accessibility::semantics::SemanticsManager>
      bindings_;

  fidl::BindingSet<fuchsia::accessibility::semantics::SemanticTree,
                   std::unique_ptr<SemanticTreeImpl>>
      semantic_tree_bindings_;

  vfs::PseudoDir* debug_dir_ = nullptr;
};
}  // namespace a11y_manager

#endif  // GARNET_BIN_A11Y_A11Y_MANAGER_SEMANTICS_SEMANTICS_MANAGER_IMPL_H_
