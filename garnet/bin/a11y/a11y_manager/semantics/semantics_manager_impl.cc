// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "semantics_manager_impl.h"

namespace a11y_manager {

void SemanticsManagerImpl::AddBinding(
    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticsManager>
        request) {
  bindings_.AddBinding(this, std::move(request));
}

void SemanticsManagerImpl::RegisterView(
    zx::event view_ref,
    fidl::InterfaceHandle<
        fuchsia::accessibility::semantics::SemanticActionListener>
        handle,
    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree>
        semantic_tree_request) {
  auto semantic_tree_impl =
      std::make_unique<SemanticTreeImpl>(std::move(view_ref));

  semantic_tree_bindings_.AddBinding(std::move(semantic_tree_impl),
                                     std::move(semantic_tree_request));
}

}  // namespace a11y_manager