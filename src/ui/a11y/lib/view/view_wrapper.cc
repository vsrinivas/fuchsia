// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "view_wrapper.h"

#include <lib/async/default.h>
namespace a11y {

ViewWrapper::ViewWrapper(
    fuchsia::ui::views::ViewRef view_ref, std::unique_ptr<SemanticTreeService> tree_service_ptr,
    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request)
    : view_ref_(std::move(view_ref)),
      semantic_tree_binding_(std::move(tree_service_ptr), std::move(semantic_tree_request)) {
  // TODO(36198): Instantiate annotation view here.
}

ViewWrapper::~ViewWrapper() { semantic_tree_binding_.Unbind(); }

void ViewWrapper::EnableSemanticUpdates(bool enabled) {
  semantic_tree_binding_.impl()->EnableSemanticsUpdates(enabled);
}

fxl::WeakPtr<::a11y::SemanticTree> ViewWrapper::GetTree() {
  return semantic_tree_binding_.impl()->Get();
}
}  // namespace a11y
