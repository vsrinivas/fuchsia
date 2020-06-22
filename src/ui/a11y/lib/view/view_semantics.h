// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_VIEW_VIEW_SEMANTICS_H_
#define SRC_UI_A11Y_LIB_VIEW_VIEW_SEMANTICS_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>

#include "src/ui/a11y/lib/semantics/semantic_tree_service.h"

namespace a11y {

// Interface for entity that manages semantics at individual view level.
class ViewSemantics {
 public:
  ViewSemantics() = default;
  virtual ~ViewSemantics() = default;
  // Close the semantics channel with the appropriate status.
  virtual void CloseChannel(zx_status_t status) = 0;

  // Turn on semantic updates for this view.
  virtual void EnableSemanticUpdates(bool enabled) = 0;

  // Returns a weak pointer to the semantic tree for this view. Caller must always check if the
  // pointer is valid before accessing, as the pointer may be invalidated. The pointer may become
  // invalidated if the semantic provider disconnects or if an error occurred. This is not thread
  // safe. This pointer may only be used in the same thread as this service is running.
  virtual fxl::WeakPtr<::a11y::SemanticTree> GetTree() = 0;
};

class ViewSemanticsFactory {
 public:
  ViewSemanticsFactory() = default;
  virtual ~ViewSemanticsFactory() = default;

  virtual std::unique_ptr<ViewSemantics> CreateViewSemantics(
      std::unique_ptr<SemanticTreeService> tree_service_ptr,
      fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree>
          semantic_tree_request) = 0;
};

}  //  namespace a11y

#endif  // SRC_UI_A11Y_LIB_VIEW_VIEW_SEMANTICS_H_
