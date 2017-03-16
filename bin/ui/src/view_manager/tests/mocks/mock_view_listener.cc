// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/tests/mocks/mock_view_listener.h"

namespace view_manager {
namespace test {

MockViewListener::MockViewListener() : callback_(nullptr) {}

MockViewListener::MockViewListener(const OnMockInvalidationCallback& callback)
    : callback_(callback) {}

MockViewListener::~MockViewListener() {}

void MockViewListener::OnInvalidation(mozart::ViewInvalidationPtr invalidation,
                                      const OnInvalidationCallback& callback) {
  if (callback_) {
    callback_(std::move(invalidation));
  }
  callback();
}

}  // namespace test
}  // namespace view_manager
