// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/tests/mocks/mock_view_listener.h"

namespace mozart {
namespace test {

MockViewListener::MockViewListener() : callback_(nullptr) {}

MockViewListener::MockViewListener(const OnMockViewPropertiesCallback& callback)
    : callback_(callback) {}

MockViewListener::~MockViewListener() {}

void MockViewListener::OnPropertiesChanged(
    mozart::ViewPropertiesPtr properties,
    const OnPropertiesChangedCallback& callback) {
  if (callback_) {
    callback_(std::move(properties));
  }
  callback();
}

}  // namespace test
}  // namespace mozart
