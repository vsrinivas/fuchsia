// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/testing/test_with_context.h>

namespace sys {
namespace testing {

TestWithContext::TestWithContext()
    : context_(ComponentContextForTest::Create()),
      controller_(&context_->controller()) {}

std::unique_ptr<ComponentContext> TestWithContext::TakeContext() {
  return std::move(context_);
}

}  // namespace testing
}  // namespace sys
