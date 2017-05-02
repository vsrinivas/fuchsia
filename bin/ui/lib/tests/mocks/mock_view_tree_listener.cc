// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/tests/mocks/mock_view_tree_listener.h"

namespace mozart {
namespace test {

MockViewTreeListener::MockViewTreeListener() : callback_(nullptr) {}

MockViewTreeListener::MockViewTreeListener(
    const OnMockRendererDiedCallback& callback)
    : callback_(callback) {}

MockViewTreeListener::~MockViewTreeListener() {}

void MockViewTreeListener::OnRendererDied(
    const OnRendererDiedCallback& callback) {
  if (callback_) {
    callback_();
  }
  callback();
}

}  // namespace test
}  // namespace mozart
