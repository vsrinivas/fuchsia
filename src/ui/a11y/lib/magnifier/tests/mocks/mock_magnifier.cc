// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/magnifier/tests/mocks/mock_magnifier.h"

#include <gtest/gtest.h>

namespace accessibility_test {

void MockMagnifier::NotImplemented_(const std::string& name) {
  FAIL() << name << " is not implemented";
}

void MockMagnifier::RegisterHandler(
    fidl::InterfaceHandle<fuchsia::accessibility::MagnificationHandler> handler) {
  handler_ = handler.Bind();
}

}  // namespace accessibility_test
