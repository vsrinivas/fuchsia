// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/default_action.h"

#include "src/lib/syslog/cpp/logger.h"

namespace a11y {

DefaultAction::DefaultAction(ActionContext* context) : action_context_(context) {}
DefaultAction::~DefaultAction() = default;

void DefaultAction::Run(ActionData process_data) {
  ExecuteHitTesting(action_context_, process_data,
                    [this, process_data](::fuchsia::accessibility::semantics::Hit hit) {
                      if (hit.has_node_id()) {
                        const auto tree_weak_ptr = GetTreePointer(action_context_, process_data);
                        if (!tree_weak_ptr) {
                          return;
                        }

                        // Call OnAccessibilityActionRequested.
                        tree_weak_ptr->PerformAccessibilityAction(
                            hit.node_id(), fuchsia::accessibility::semantics::Action::DEFAULT,
                            [](bool result) {
                              FX_LOGS(INFO) << "Default Action completed with status:" << result;
                            });
                      } else {
                        FX_LOGS(INFO) << "DefaultAction: Node id is missing in the hit result.";
                      }
                    });
}

}  // namespace a11y
