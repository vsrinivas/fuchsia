// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/threading/create_thread.h"

#include "gtest/gtest.h"
#include "lib/mtl/handles/object_info.h"
#include "lib/mtl/tasks/message_loop.h"

namespace mtl {
namespace {

TEST(CreateThread, DefaultName) {
  fxl::RefPtr<fxl::TaskRunner> task_runner;
  std::thread child = CreateThread(&task_runner);

  bool task_ran = false;
  task_runner->PostTask([&task_ran]() {
    task_ran = true;
    EXPECT_EQ("message loop", mtl::GetCurrentThreadName());
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
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
    EXPECT_EQ("custom name", mtl::GetCurrentThreadName());
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  });

  child.join();
  EXPECT_TRUE(task_ran);
}

}  // namespace
}  // namespace mtl
