// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_UI_A11Y_LIB_VIEW_VIEW_WRAPPER_H_
#define SRC_UI_A11Y_LIB_VIEW_VIEW_WRAPPER_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding.h>

#include <optional>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/a11y/lib/annotation/annotation_view.h"
#include "src/ui/a11y/lib/semantics/semantic_tree_service.h"
#include "src/ui/a11y/lib/semantics/util/semantic_transform.h"
#include "src/ui/a11y/lib/view/view_semantics.h"
#include "src/ui/input/lib/injector/injector.h"

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

  ~ViewWrapper();

  // Returns a weak pointer to `this`.
  fxl::WeakPtr<ViewWrapper> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // Returns a clone of the ViewRef owned by this object.
  fuchsia::ui::views::ViewRef ViewRefClone() const;

  ViewSemantics* view_semantics() { return view_semantics_.get(); }

  AnnotationViewInterface* annotation_view() { return annotation_view_.get(); }

  // Returns the injector for the view associated with this class. If no injector was registered,
  // turns nullptr.
  input::Injector* view_injector() { return view_injector_.get(); }

  void set_view_injector(std::shared_ptr<input::Injector> view_injector) {
    view_injector_ = std::move(view_injector);
  }

  // Takes the injector (if any), owned by this class and resets the object owned by this class to
  // nullptr.
  std::shared_ptr<input::Injector> take_view_injector();

 private:
  fuchsia::ui::views::ViewRef view_ref_;

  // Responsible for semantic operations in this view.
  std::unique_ptr<ViewSemantics> view_semantics_;

  // View used to draw annotations over semantic view.
  std::unique_ptr<AnnotationViewInterface> annotation_view_;

  // Used to inject pointer events into the view.
  std::shared_ptr<input::Injector> view_injector_;

  // Used to generate weak pointers to `this`.
  fxl::WeakPtrFactory<ViewWrapper> weak_factory_;
};

}  // namespace a11y
#endif  // SRC_UI_A11Y_LIB_VIEW_VIEW_WRAPPER_H_
