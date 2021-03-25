// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/annotation/tests/mocks/mock_focus_highlight_manager.h"

namespace accessibility_test {

void MockFocusHighlightManager::SetAnnotationsEnabled(bool annotations_enabled) {
  annotations_enabled_ = annotations_enabled;
}

bool MockFocusHighlightManager::GetAnnotationsEnabled() { return annotations_enabled_; }

void MockFocusHighlightManager::ClearAllHighlights() {
  ClearFocusHighlights();
  ClearMagnificationHighlights();
}

void MockFocusHighlightManager::ClearFocusHighlights() { highlighted_node_ = std::nullopt; }

void MockFocusHighlightManager::ClearMagnificationHighlights() {
  magnification_scale_ = std::nullopt;
  magnification_translation_x_ = std::nullopt;
  magnification_translation_y_ = std::nullopt;
  magnification_koid_ = std::nullopt;
}

void MockFocusHighlightManager::HighlightMagnificationViewport(zx_koid_t koid,
                                                               float magnification_scale,
                                                               float magnification_translation_x,
                                                               float magnification_translation_y) {
  magnification_koid_ = std::make_optional<zx_koid_t>(koid);
  magnification_translation_x_ = std::make_optional<float>(magnification_translation_x);
  magnification_translation_y_ = std::make_optional<float>(magnification_translation_y);
  magnification_scale_ = std::make_optional<float>(magnification_scale);
}

void MockFocusHighlightManager::UpdateHighlight(SemanticNodeIdentifier newly_highlighted_node) {
  highlighted_node_ = newly_highlighted_node;
}

void MockFocusHighlightManager::UpdateMagnificationHighlights(zx_koid_t koid) {
  magnification_koid_ = koid;
}

std::optional<a11y::FocusHighlightManager::SemanticNodeIdentifier>
MockFocusHighlightManager::GetHighlightedNode() const {
  return highlighted_node_;
}

std::optional<zx_koid_t> MockFocusHighlightManager::GetMagnificationHighlightKoid() const {
  return magnification_koid_;
}

std::optional<float> MockFocusHighlightManager::GetMagnificationHighlightScale() const {
  return magnification_scale_;
}

std::optional<float> MockFocusHighlightManager::GetMagnificationHighlightTranslationX() const {
  return magnification_translation_x_;
}

std::optional<float> MockFocusHighlightManager::GetMagnificationHighlightTranslationY() const {
  return magnification_translation_y_;
}

}  // namespace accessibility_test
