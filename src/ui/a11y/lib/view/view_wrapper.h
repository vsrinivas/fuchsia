// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_UI_A11Y_LIB_VIEW_VIEW_WRAPPER_H_
#define SRC_UI_A11Y_LIB_VIEW_VIEW_WRAPPER_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding.h>

#include <optional>

#include "src/ui/a11y/lib/annotation/annotation_view.h"
#include "src/ui/a11y/lib/semantics/semantic_tree_service.h"
#include "src/ui/a11y/lib/view/view_semantics.h"

namespace a11y {

// A wrapper for the accessibility manager's objects associated with a particular scenic view. When
// the associated viewRef becomes invalid this object should be destroyed. The owner of this object
// is responsible for watching signals on the associated viewRef and destroying this object when the
// viewRef goes out of scope.
class ViewWrapper {
 public:
  // Creates a Wrapper for this view and binds the associated semantic tree.
  ViewWrapper(fuchsia::ui::views::ViewRef view_ref, std::unique_ptr<ViewSemantics> view_semantics,
              std::unique_ptr<AnnotationViewInterface> annotation_view);

  ~ViewWrapper() = default;

  // Close the semantics channel with the appropriate status.
  void CloseChannel(zx_status_t status) { view_semantics_->CloseChannel(status); }

  // Turn on semantic updates for this view.
  void EnableSemanticUpdates(bool enabled);

  // Returns a weak pointer to the Semantic Tree. Caller must always check if the pointer is valid
  // before accessing, as the pointer may be invalidated. The pointer may become invalidated if the
  // semantic provider disconnects or if an error occurred. This is not thread safe. This pointer
  // may only be used in the same thread as this service is running.
  fxl::WeakPtr<::a11y::SemanticTree> GetTree();

  // Returns a clone of the ViewRef owned by this object.
  fuchsia::ui::views::ViewRef ViewRefClone() const;

  // Highlights node with id |node_id|.
  void HighlightNode(uint32_t node_id);

  // Clears contents of annotation view.
  void ClearHighlights();

 private:
  fuchsia::ui::views::ViewRef view_ref_;

  // Responsible for semantic operations in this view.
  std::unique_ptr<ViewSemantics> view_semantics_;

  // View used to draw annotations over semantic view.
  std::unique_ptr<AnnotationViewInterface> annotation_view_;
};

}  // namespace a11y
#endif  // SRC_UI_A11Y_LIB_VIEW_VIEW_WRAPPER_H_
