// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/annotation/tests/mocks/mock_focus_highlight_manager.h"

namespace accessibility_test {

void MockFocusHighlightManager::SetAnnotationsEnabled(bool annotations_enabled) {
  annotations_enabled_ = annotations_enabled;
}

bool MockFocusHighlightManager::GetAnnotationsEnabled() { return annotations_enabled_; }

void MockFocusHighlightManager::ClearAllHighlights() { ClearFocusHighlights(); }

void MockFocusHighlightManager::ClearFocusHighlights() { highlighted_node_ = std::nullopt; }

void MockFocusHighlightManager::UpdateHighlight(SemanticNodeIdentifier newly_highlighted_node) {
  highlighted_node_ = newly_highlighted_node;
}

std::optional<a11y::FocusHighlightManager::SemanticNodeIdentifier>
MockFocusHighlightManager::GetHighlightedNode() const {
  return highlighted_node_;
}

}  // namespace accessibility_test
