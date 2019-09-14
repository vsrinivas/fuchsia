// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock_setui_accessibility.h"

#include <lib/sys/cpp/testing/component_context_provider.h>

namespace accessibility_test {

MockSetUIAccessibility::MockSetUIAccessibility(sys::testing::ComponentContextProvider* context) {
  context->service_directory_provider()->AddService(bindings_.GetHandler(this));
}

MockSetUIAccessibility::~MockSetUIAccessibility() = default;

void MockSetUIAccessibility::Watch(WatchCallback callback) { watchCallback_ = std::move(callback); }

void MockSetUIAccessibility::Set(fuchsia::settings::AccessibilitySettings settings,
                                 SetCallback callback) {
  if (watchCallback_) {
    fuchsia::settings::Accessibility_Watch_Result result;
    result.set_response({.settings = std::move(settings)});
    watchCallback_(std::move(result));
  }
  callback({});
}
}  // namespace accessibility_test
