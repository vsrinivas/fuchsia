// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_ANNOTATION_FOCUS_HIGHLIGHT_MANAGER_H_
#define SRC_UI_A11Y_LIB_ANNOTATION_FOCUS_HIGHLIGHT_MANAGER_H_

#include <zircon/types.h>

namespace a11y {

// An interface for manipulating a11y focus highlights.
class FocusHighlightManager {
 public:
  struct SemanticNodeIdentifier {
    zx_koid_t koid;
    uint32_t node_id;
  };

  FocusHighlightManager() = default;
  virtual ~FocusHighlightManager() = default;

  // Enables/disables annotations.
  virtual void SetAnnotationsEnabled(bool annotations_enabled) = 0;

  // Clears existing highlights.
  virtual void ClearAllHighlights() = 0;
  virtual void ClearFocusHighlights() = 0;
  virtual void ClearMagnificationHighlights() = 0;

  // Draws a highlight around the boundary of the magnified viewport.
  // |scale|, |translation_x|, and |translation_y| specify the clip space
  // transform, which is a transfom applied to the NDC space
  // (scale-then-translate).
  virtual void HighlightMagnificationViewport(zx_koid_t koid, float magnification_scale,
                                              float magnification_translation_x,
                                              float magnification_translation_y) = 0;

  // Clears existing highlight (if any) and draws a highlight around |newly_highlighted_node.|
  virtual void UpdateHighlight(SemanticNodeIdentifier newly_highlighted_node) = 0;

  // Clears existing magnification highlight and draws a new one (if any).
  virtual void UpdateMagnificationHighlights(zx_koid_t koid) = 0;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_ANNOTATION_FOCUS_HIGHLIGHT_MANAGER_H_
