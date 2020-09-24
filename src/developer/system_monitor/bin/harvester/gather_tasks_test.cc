// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_tasks.h"

#include <zircon/process.h>

#include <gtest/gtest.h>

#include "dockyard_proxy_fake.h"
#include "root_resource.h"

class GatherTasksTest : public ::testing::Test {
 public:
  void SetUp() override {
    // Determine our KOID.
    zx_info_handle_basic_t info;
    zx_status_t status = zx_object_get_info(
        zx_process_self(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
        /*actual=*/nullptr, /*avail=*/nullptr);
    ASSERT_EQ(status, ZX_OK);
    self_koid_ = std::to_string(info.koid);
  }

  // Get a dockyard path for our koid with the given |suffix| key.
  std::string KoidPath(const std::string& suffix) {
    std::ostringstream out;
    out << "koid:" << self_koid_ << ":" << suffix;
    return out.str();
  }

 private:
  std::string self_koid_;
};

TEST_F(GatherTasksTest, MemoryData) {
  zx_handle_t root_resource;
  ASSERT_EQ(harvester::GetRootResource(&root_resource), ZX_OK);
  harvester::DockyardProxyFake dockyard_proxy;
  harvester::GatherTasks gatherer(root_resource, &dockyard_proxy);
  gatherer.Gather();

  std::string test_string;
  EXPECT_TRUE(dockyard_proxy.CheckStringSent(KoidPath("name"), &test_string));
  // This is the name of our generated test process. If the testing harness
  // changes this may need to be updated. The intent is to test for a process
  // that is running.
  EXPECT_EQ("system_monitor_harvester_test.c", test_string);

  // For the moment we send both mapped_bytes and private_scaled_shared_bytes.
  // Once the front-end no longer expects memory_mapped_bytes that value will
  // go away (fxbug.dev/43887).
  EXPECT_TRUE(dockyard_proxy.CheckValueSubstringSent("memory_mapped_bytes"));
  EXPECT_TRUE(dockyard_proxy.CheckValueSubstringSent(
      "memory_private_scaled_shared_bytes"));
}
