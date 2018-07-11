// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/add_mod_command_runner.h"

#include <lib/gtest/test_loop_fixture.h>

#include "gtest/gtest.h"

namespace modular {
namespace {

class AddModCommandRunnerTest : public gtest::TestLoopFixture {
 protected:
  std::unique_ptr<AddModCommandRunner> runner_{nullptr /* session_storage */};
};

TEST_F(AddModCommandRunnerTest, EmptyTest) {}

}  // namespace
}  // namespace modular
