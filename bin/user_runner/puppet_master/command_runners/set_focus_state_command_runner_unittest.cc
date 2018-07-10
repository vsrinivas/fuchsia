// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/set_focus_state_command_runner.h"

#include <lib/gtest/test_loop_fixture.h>

#include "gtest/gtest.h"

namespace modular {
namespace {

class SetFocusStateCommandRunnerTest : public gtest::TestLoopFixture {
 protected:
  std::unique_ptr<SetFocusStateCommandRunner> runner_;
};

TEST_F(SetFocusStateCommandRunnerTest, EmptyTest) {}

}  // namespace
}  // namespace modular
