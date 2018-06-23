// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/testing/test_with_context.h"

namespace fuchsia {
namespace sys {
namespace testing {

TestWithContext::TestWithContext()
    : context_(StartupContextForTest::Create()),
      controller_(&context_->controller()) {
  // Take the real StartupContext to prevent code under test from having it
  fuchsia::sys::StartupContext::CreateFromStartupInfo();
}

std::unique_ptr<StartupContext> TestWithContext::TakeContext() {
  return std::move(context_);
}

}  // namespace testing
}  // namespace sys
}  // namespace fuchsia
