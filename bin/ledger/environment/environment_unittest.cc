// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/environment/environment.h"

#include "gtest/gtest.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {
namespace {

TEST(Environment, NoIOThread) {
  // Check that the environment without an io_thread can be deleted correctly.
  Environment env(configuration::Configuration(), nullptr, nullptr);
}

TEST(Environment, GivenIOThread) {
  mtl::MessageLoop loop;
  Environment env(configuration::Configuration(), nullptr, nullptr,
                  loop.task_runner());

  EXPECT_EQ(loop.task_runner(), env.GetIORunner());
}

TEST(Environment, DefaultIOThread) {
  int value = 0;
  {
    Environment env(configuration::Configuration(), nullptr, nullptr);
    auto io_runner = env.GetIORunner();
    io_runner->PostTask([&value] { value = 1; });
  }
  EXPECT_EQ(1, value);
}

}  // namespace
}  // namespace ledger
