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
namespace a11y {

// A wrapper for the accessibility manager's objects associated with a particular scenic view. When
// the associated viewRef becomes invalid this object should be destroyed. The owner of this object
// is responsible for watching signals on the associated viewRef and destroying this object when the
// viewRef goes out of scope.
class ViewWrapper {
 public:
  // Maintains state of a11y annotations in this view.
  struct AnnotationState {
    // True when this view holds a11y focus and false otherwise.
    bool has_annotations = false;

    // Stores id of a11y-focused node within this view when this view holds a11y focus.
    std::optional<uint32_t> annotated_node_id = std::nullopt;
  };

  // Creates a Wrapper for this view and binds the associated semantic tree.
  ViewWrapper(
      fuchsia::ui::views::ViewRef view_ref, std::unique_ptr<SemanticTreeService> tree_service_ptr,
      fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request,
      // TODO: Remove default values once user classes have been updated.
      sys::ComponentContext* context = nullptr,
      std::unique_ptr<AnnotationViewFactoryInterface> annotation_view_factory = nullptr);

  virtual ~ViewWrapper();

  // Turn on semantic updates for this view.
  virtual void EnableSemanticUpdates(bool enabled);

  // Returns a weak pointer to the Semantic Tree. Caller must always check if the pointer is valid
  // before accessing, as the pointer may be invalidated. The pointer may become invalidated if the
  // semantic provider disconnects or if an error occurred. This is not thread safe. This pointer
  // may only be used in the same thread as this service is running.
  virtual fxl::WeakPtr<::a11y::SemanticTree> GetTree();

  // Returns a clone of the ViewRef owned by this object.
  virtual fuchsia::ui::views::ViewRef ViewRefClone() const;

  // Highlights node with id |node_id|.
  virtual void HighlightNode(uint32_t node_id);

  // Clears contents of annotation view.
  virtual void ClearHighlights();

 private:
  // Helper function to draw highlight arround currently annotated node.
  void DrawHighlight();

  // Helper function to hide highlights.
  void HideHighlights();

  AnnotationState annotation_state_;

  fuchsia::ui::views::ViewRef view_ref_;

  fidl::Binding<fuchsia::accessibility::semantics::SemanticTree,
                std::unique_ptr<SemanticTreeService>>
      semantic_tree_binding_;

  // Used to instantiate annotation view.
  std::unique_ptr<AnnotationViewFactoryInterface> annotation_view_factory_;

  // View used to draw annotations over semantic view.
  std::unique_ptr<AnnotationViewInterface> annotation_view_;
};

class ViewWrapperFactory {
 public:
  ViewWrapperFactory() = default;
  virtual ~ViewWrapperFactory() = default;

  virtual std::unique_ptr<ViewWrapper> CreateViewWrapper(
      fuchsia::ui::views::ViewRef view_ref, std::unique_ptr<SemanticTreeService> tree_service_ptr,
      fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree>
          semantic_tree_request);
};

}  // namespace a11y
#endif  // SRC_UI_A11Y_LIB_VIEW_VIEW_WRAPPER_H_
