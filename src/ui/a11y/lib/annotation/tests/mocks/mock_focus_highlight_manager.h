// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_ANNOTATION_TESTS_MOCKS_MOCK_FOCUS_HIGHLIGHT_MANAGER_H_
#define SRC_UI_A11Y_LIB_ANNOTATION_TESTS_MOCKS_MOCK_FOCUS_HIGHLIGHT_MANAGER_H_

#include <fuchsia/ui/views/cpp/fidl.h>

#include <map>
#include <optional>

#include "src/ui/a11y/lib/annotation/focus_highlight_manager.h"

namespace accessibility_test {

class MockFocusHighlightManager : public a11y::FocusHighlightManager {
 public:
  MockFocusHighlightManager() = default;
  ~MockFocusHighlightManager() override = default;

  // |FocusHighlightManager|
  void SetAnnotationsEnabled(bool annotations_enabled) override;

  // Returns value of |annotations_enabled_|.
  bool GetAnnotationsEnabled();

  // |FocusHighlightManager|
  void ClearAllHighlights() override;
  void ClearFocusHighlights() override;
  void ClearMagnificationHighlights() override;

  // |FocusHighlightManager|
  void HighlightMagnificationViewport(zx_koid_t koid, float magnification_scale,
                                      float magnification_translation_x,
                                      float magnification_translation_y) override;

  // |FocusHighlightManager|
  void UpdateHighlight(SemanticNodeIdentifier newly_highlighted_node) override;

  // |FocusHighlightManager|
  void UpdateMagnificationHighlights(zx_koid_t koid) override;

  // Returns currently highlighted node.
  std::optional<SemanticNodeIdentifier> GetHighlightedNode() const;

 private:
  bool annotations_enabled_ = false;

  std::optional<SemanticNodeIdentifier> highlighted_node_;
  std::optional<zx_koid_t> magnification_koid_;
  std::optional<float> magnification_scale_;
  std::optional<float> magnification_translation_x_;
  std::optional<float> magnification_translation_y_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_ANNOTATION_TESTS_MOCKS_MOCK_FOCUS_HIGHLIGHT_MANAGER_H_
