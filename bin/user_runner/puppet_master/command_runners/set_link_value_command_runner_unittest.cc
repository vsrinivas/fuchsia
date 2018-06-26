// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/set_link_value_command_runner.h"

#include "gtest/gtest.h"
#include "lib/gtest/test_with_loop.h"

namespace modular {
namespace {

class SetLinkValueCommandRunnerTest : public gtest::TestWithLoop {
 protected:
  std::unique_ptr<SetLinkValueCommandRunner> runner_;
};

TEST_F(SetLinkValueCommandRunnerTest, EmptyTest) {
}

}  // namespace
}  // namespace modular
