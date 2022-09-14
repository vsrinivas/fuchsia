// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxdump/task.h>

#include <gtest/gtest.h>

#include "dump-tests.h"
#include "rights.h"

namespace {

TEST(ZxdumpTests, TaskBasic) {
  zxdump::testing::TestProcess process;
  constexpr const char* kChildName = "task-basic-test";

  ASSERT_NO_FATAL_FAILURE(process.SpawnAction({
      .action = FDIO_SPAWN_ACTION_SET_NAME,
      .name = {kChildName},
  }));
  ASSERT_NO_FATAL_FAILURE(process.StartChild());

  zxdump::TaskHolder holder;
  auto insert_result = holder.Insert(process.handle());
  ASSERT_TRUE(insert_result.is_ok()) << insert_result.error_value();

  // The dump has no jobs, so there should be a placeholder "super-root".
  EXPECT_EQ(ZX_KOID_INVALID, holder.root_job().koid());

  auto processes = holder.root_job().processes();
  ASSERT_TRUE(processes.is_ok()) << processes.error_value();

  // The fake job should have exactly one process.
  EXPECT_EQ(processes->get().size(), 1u);
  for (auto& [read_koid, read_process] : processes->get()) {
    EXPECT_NE(read_koid, ZX_KOID_INVALID);

    // Get the basic info from the real live process handle.
    zx_info_handle_basic_t basic;
    ASSERT_EQ(ZX_OK, process.borrow()->get_info(ZX_INFO_HANDLE_BASIC, &basic, sizeof(basic),
                                                nullptr, nullptr));
    EXPECT_EQ(read_koid, basic.koid);
    EXPECT_EQ(ZX_OBJ_TYPE_PROCESS, basic.type);

    auto read_basic = read_process.get_info<ZX_INFO_HANDLE_BASIC>();
    ASSERT_TRUE(read_basic.is_ok()) << read_basic.error_value();
    EXPECT_EQ(basic.koid, read_basic->koid);
    EXPECT_EQ(basic.rights, read_basic->rights);
    EXPECT_EQ(basic.type, read_basic->type);
    EXPECT_EQ(basic.related_koid, read_basic->related_koid);

    auto read_name = read_process.get_property<ZX_PROP_NAME>();
    ASSERT_TRUE(read_name.is_ok()) << read_name.error_value();
    EXPECT_STREQ(kChildName, read_name->data());

    auto threads = read_process.threads();
    ASSERT_TRUE(threads.is_ok()) << threads.error_value();

    // The test process has only one thread.
    EXPECT_EQ(threads->get().size(), 1u);
    for (auto& [thread_koid, thread] : threads->get()) {
      zx::handle child_handle;
      ASSERT_EQ(ZX_OK,
                process.borrow()->get_child(thread_koid, zxdump::kThreadRights, &child_handle));
      ASSERT_EQ(ZX_OK, child_handle.get_info(ZX_INFO_HANDLE_BASIC, &basic, sizeof(basic), nullptr,
                                             nullptr));
      read_basic = thread.get_info<ZX_INFO_HANDLE_BASIC>();
      ASSERT_TRUE(read_basic.is_ok()) << read_basic.error_value();
      EXPECT_EQ(basic.koid, read_basic->koid);
      EXPECT_EQ(basic.type, read_basic->type);
      EXPECT_EQ(basic.related_koid, read_basic->related_koid);
    }
  }
}

}  // namespace
