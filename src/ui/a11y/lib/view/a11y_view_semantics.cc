// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/ui/a11y/lib/view/a11y_view_semantics.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

namespace a11y {

A11yViewSemantics::A11yViewSemantics(
    std::unique_ptr<SemanticTreeService> tree_service_ptr,
    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request)
    : semantic_tree_binding_(std::move(tree_service_ptr), std::move(semantic_tree_request)) {}

A11yViewSemantics::~A11yViewSemantics() { semantic_tree_binding_.Unbind(); }

void A11yViewSemantics::EnableSemanticUpdates(bool enabled) {
  semantic_tree_binding_.impl()->EnableSemanticsUpdates(enabled);
}

fxl::WeakPtr<::a11y::SemanticTree> A11yViewSemantics::GetTree() {
  return semantic_tree_binding_.impl()->Get();
}

std::unique_ptr<ViewSemantics> A11yViewSemanticsFactory::CreateViewSemantics(
    std::unique_ptr<SemanticTreeService> tree_service_ptr,
    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request) {
  return std::make_unique<A11yViewSemantics>(std::move(tree_service_ptr),
                                             std::move(semantic_tree_request));
}

}  // namespace a11y
