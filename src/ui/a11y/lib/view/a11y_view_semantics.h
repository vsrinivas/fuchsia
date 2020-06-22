/// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_UI_A11Y_LIB_VIEW_A11Y_VIEW_SEMANTICS_H_
#define SRC_UI_A11Y_LIB_VIEW_A11Y_VIEW_SEMANTICS_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding.h>

#include <optional>

#include "src/ui/a11y/lib/view/view_semantics.h"

namespace a11y {

class A11yViewSemantics : public ViewSemantics {
 public:
  A11yViewSemantics(std::unique_ptr<SemanticTreeService> tree_service_ptr,
                    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree>
                        semantic_tree_request);

  ~A11yViewSemantics() override;

  // |ViewSemanticsManager|
  void CloseChannel(zx_status_t status) override { semantic_tree_binding_.Close(status); }

  // |ViewSemanticsManager|
  void EnableSemanticUpdates(bool enabled) override;

  // TODO: Deprecate.
  // |ViewSemanticsManager|
  fxl::WeakPtr<::a11y::SemanticTree> GetTree() override;

 private:
  fidl::Binding<fuchsia::accessibility::semantics::SemanticTree,
                std::unique_ptr<SemanticTreeService>>
      semantic_tree_binding_;
};

class A11yViewSemanticsFactory : public ViewSemanticsFactory {
 public:
  A11yViewSemanticsFactory() = default;
  ~A11yViewSemanticsFactory() override = default;

  std::unique_ptr<ViewSemantics> CreateViewSemantics(
      std::unique_ptr<SemanticTreeService> tree_service_ptr,
      fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request)
      override;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_VIEW_A11Y_VIEW_SEMANTICS_H_
