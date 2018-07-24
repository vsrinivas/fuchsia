// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/tests/mocks/mock_view_container_listener.h"

namespace mozart {
namespace test {

MockViewContainerListener::MockViewContainerListener()
    : child_attached_callback_(nullptr), child_unavailable_callback_(nullptr) {}

MockViewContainerListener::MockViewContainerListener(
    OnMockChildAttachedCallback child_attached_callback,
    OnMockChildUnavailable child_unavailable_callback)
    : child_attached_callback_(child_attached_callback),
      child_unavailable_callback_(child_unavailable_callback) {}

MockViewContainerListener::~MockViewContainerListener() {}

void MockViewContainerListener::OnChildAttached(
    uint32_t child_key, ::fuchsia::ui::viewsv1::ViewInfo child_view_info,
    OnChildAttachedCallback callback) {
  if (child_attached_callback_) {
    child_attached_callback_(child_key, std::move(child_view_info));
  }
  callback();
}

void MockViewContainerListener::OnChildUnavailable(
    uint32_t child_key, OnChildUnavailableCallback callback) {
  if (child_unavailable_callback_) {
    child_unavailable_callback_(child_key);
  }
  callback();
}

}  // namespace test
}  // namespace mozart
