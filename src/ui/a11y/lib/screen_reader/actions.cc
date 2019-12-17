// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/actions.h"

namespace a11y {

ScreenReaderAction::ScreenReaderAction() = default;
ScreenReaderAction::~ScreenReaderAction() = default;

fxl::WeakPtr<::a11y::SemanticTree> ScreenReaderAction::GetTreePointer(ActionContext* context,
                                                                      ActionData data) {
  FXL_DCHECK(context);
  return context->semantics_manager->GetTreeByKoid(data.koid);
}

void ScreenReaderAction::ExecuteHitTesting(
    ActionContext* context, ActionData process_data,
    fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) {
  FXL_DCHECK(context);
  const auto tree_weak_ptr = GetTreePointer(context, process_data);
  if (!tree_weak_ptr) {
    return;
  }

  tree_weak_ptr->PerformHitTesting(process_data.local_point, std::move(callback));
}

}  // namespace a11y
