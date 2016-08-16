// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/threading/create_thread.h"
#include "lib/mtl/tasks/message_loop.h"
#include "gtest/gtest.h"

namespace mtl {
namespace {

TEST(CreateThread, Control) {
  ftl::RefPtr<ftl::TaskRunner> task_runner;
  std::thread child = CreateThread(&task_runner);

  bool task_ran = false;
  task_runner->PostTask([&task_ran]() {
    task_ran = true;
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  });

  child.join();
  EXPECT_TRUE(task_ran);
}

}  // namespace
}  // namespace mtl
