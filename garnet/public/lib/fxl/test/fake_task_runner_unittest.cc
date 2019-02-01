// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/test/fake_task_runner.h"

#include "gtest/gtest.h"

namespace fxl {
namespace {

TEST(FakeTaskRunnerTest, MultipleTasks) {
  auto runner = MakeRefCounted<FakeTaskRunner>();
  int count = 0;
  auto increment = [&count] { count++; };

  runner->PostTask(increment);
  runner->PostTask(increment);
  ASSERT_EQ(0, count);
  runner->Run();
  ASSERT_EQ(2, count);
}

TEST(FakeTaskRunnerTest, QuitAndRestart) {
  auto runner = MakeRefCounted<FakeTaskRunner>();
  int count = 0;
  auto increment = [&count] { count++; };
  auto quit = [&runner] { runner->QuitNow(); };

  runner->PostTask(increment);
  runner->PostTask(quit);
  runner->PostTask(increment);
  runner->Run();
  ASSERT_EQ(1, count);
  runner->Run();
  ASSERT_EQ(2, count);
}

}  // namespace
}  // namespace fxl
