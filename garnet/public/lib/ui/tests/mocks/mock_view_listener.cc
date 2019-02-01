// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/tests/mocks/mock_view_listener.h"

#include <utility>

namespace mozart {
namespace test {

MockViewListener::MockViewListener() : callback_(nullptr) {}

MockViewListener::MockViewListener(OnMockViewPropertiesCallback callback)
    : callback_(std::move(callback)) {}

MockViewListener::~MockViewListener() {}

void MockViewListener::OnPropertiesChanged(
    ::fuchsia::ui::viewsv1::ViewProperties properties,
    OnPropertiesChangedCallback callback) {
  if (callback_) {
    callback_(std::move(properties));
  }
  callback();
}

}  // namespace test
}  // namespace mozart
