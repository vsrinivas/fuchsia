// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_UI_A11Y_LIB_VIEW_VIEW_WRAPPER_H_
#define SRC_UI_A11Y_LIB_VIEW_VIEW_WRAPPER_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding.h>

#include "src/ui/a11y/lib/semantics/semantic_tree_service.h"
namespace a11y {

// A wrapper for the accessibility manager's objects associated with a particular scenic view. When
// the associated viewRef becomes invalid this object should be destroyed. The owner of this object
// is responsible for watching signals on the associated viewRef and destroying this object when the
// viewRef goes out of scope.
class ViewWrapper {
 public:
  // Creates a Wrapper for this view and binds the associated semantic tree.
  ViewWrapper(fuchsia::ui::views::ViewRef view_ref,
              std::unique_ptr<SemanticTreeService> tree_service_ptr,
              fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree>
                  semantic_tree_request);

  ~ViewWrapper();

  // Turn on semantic updates for this view.
  void EnableSemanticUpdates(bool enabled);

  // Returns a weak pointer to the Semantic Tree. Caller must always check if the pointer is valid
  // before accessing, as the pointer may be invalidated. The pointer may become invalidated if the
  // semantic provider disconnects or if an error occurred. This is not thread safe. This pointer
  // may only be used in the same thread as this service is running.
  fxl::WeakPtr<::a11y::SemanticTree> GetTree();

  // Returns a clone of the ViewRef owned by this object.
  fuchsia::ui::views::ViewRef ViewRefClone() const;

 private:
  fuchsia::ui::views::ViewRef view_ref_;

  fidl::Binding<fuchsia::accessibility::semantics::SemanticTree,
                std::unique_ptr<SemanticTreeService>>
      semantic_tree_binding_;

  // TODO(36198): Add annotation view here.
};
}  // namespace a11y
#endif  // SRC_UI_A11Y_LIB_VIEW_VIEW_WRAPPER_H_
