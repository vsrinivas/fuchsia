// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_listener.h"

#include <lib/syslog/cpp/logger.h>

namespace accessibility_test {

void MockSemanticListener::Bind(
    fidl::InterfaceHandle<fuchsia::accessibility::semantics::SemanticListener> *listener) {
  semantic_listener_bindings_.AddBinding(this, listener->NewRequest());
}

void MockSemanticListener::SetHitTestResult(int node_id) { hit_test_node_id_ = node_id; }

void MockSemanticListener::HitTest(::fuchsia::math::PointF local_point, HitTestCallback callback) {
  fuchsia::accessibility::semantics::Hit hit;
  hit.set_node_id(hit_test_node_id_);
  hit.mutable_path_from_root()->push_back(hit_test_node_id_);
  callback(std::move(hit));
}

}  // namespace accessibility_test
