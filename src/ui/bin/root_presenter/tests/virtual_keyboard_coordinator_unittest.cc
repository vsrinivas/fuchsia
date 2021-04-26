// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/virtual_keyboard_coordinator.h"

#include <lib/sys/cpp/testing/component_context_provider.h>

#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/test_loop_fixture.h>

namespace root_presenter {
namespace virtual_keyboard_coordinator {
namespace {

class VirtualKeyboardCoordinatorTest : public gtest::TestLoopFixture {
 protected:
  auto* context_provider() { return &context_provider_; }

 private:
  sys::testing::ComponentContextProvider context_provider_;
};

TEST_F(VirtualKeyboardCoordinatorTest, CtorDoesNotCrash) {
  VirtualKeyboardCoordinator coordinator(context_provider()->context());
}

}  // namespace
}  // namespace virtual_keyboard_coordinator
}  // namespace root_presenter
