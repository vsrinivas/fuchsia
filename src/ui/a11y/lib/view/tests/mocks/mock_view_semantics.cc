// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/view/tests/mocks/mock_view_semantics.h"

namespace accessibility_test {

MockViewSemantics::MockViewSemantics(
    std::unique_ptr<a11y::SemanticTreeService> tree_service_ptr,
    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request)
    : semantic_tree_binding_(std::move(tree_service_ptr), std::move(semantic_tree_request)) {}

MockViewSemantics::~MockViewSemantics() { semantic_tree_binding_.Unbind(); }

void MockViewSemantics::EnableSemanticUpdates(bool enabled) {
  semantic_tree_binding_.impl()->EnableSemanticsUpdates(enabled);
  semantics_enabled_ = enabled;
}

fxl::WeakPtr<::a11y::SemanticTree> MockViewSemantics::GetTree() {
  return semantic_tree_binding_.impl()->Get();
}

std::unique_ptr<a11y::ViewSemantics> MockViewSemanticsFactory::CreateViewSemantics(
    std::unique_ptr<a11y::SemanticTreeService> tree_service_ptr,
    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request) {
  auto mock_view_semantics = std::make_unique<MockViewSemantics>(std::move(tree_service_ptr),
                                                                 std::move(semantic_tree_request));
  view_semantics_ = mock_view_semantics.get();

  return mock_view_semantics;
}

MockViewSemantics* MockViewSemanticsFactory::GetViewSemantics() { return view_semantics_; }

}  // namespace accessibility_test
