// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/threading/create_thread.h"

#include "gtest/gtest.h"
#include "lib/fsl/handles/object_info.h"
#include "lib/fsl/tasks/message_loop.h"

namespace fsl {
namespace {

TEST(CreateThread, DefaultName) {
  fxl::RefPtr<fxl::TaskRunner> task_runner;
  std::thread child = CreateThread(&task_runner);

  bool task_ran = false;
  task_runner->PostTask([&task_ran]() {
    task_ran = true;
    EXPECT_EQ("message loop", fsl::GetCurrentThreadName());
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
  });

  child.join();
  EXPECT_TRUE(task_ran);
}

TEST(CreateThread, CustomName) {
  fxl::RefPtr<fxl::TaskRunner> task_runner;
  std::thread child = CreateThread(&task_runner, "custom name");

  bool task_ran = false;
  task_runner->PostTask([&task_ran]() {
    task_ran = true;
    EXPECT_EQ("custom name", fsl::GetCurrentThreadName());
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
  });

  child.join();
  EXPECT_TRUE(task_ran);
}

}  // namespace
}  // namespace fsl
