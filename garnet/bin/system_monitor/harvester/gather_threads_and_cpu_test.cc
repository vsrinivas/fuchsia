// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_threads_and_cpu.h"

#include <zircon/process.h>

#include "dockyard_proxy_fake.h"
#include "gtest/gtest.h"
#include "root_resource.h"
#include "src/lib/fxl/logging.h"

class GatherThreadsAndCpuTest : public ::testing::Test {
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

TEST_F(GatherThreadsAndCpuTest, Inspectable) {
  zx_handle_t root_resource;
  ASSERT_EQ(harvester::GetRootResource(&root_resource), ZX_OK);
  harvester::DockyardProxyFake dockyard_proxy;
  harvester::GatherThreadsAndCpu gatherer(root_resource, &dockyard_proxy);
  gatherer.Gather();

  std::string test_string;
  EXPECT_TRUE(dockyard_proxy.CheckStringSent(KoidPath("name"), &test_string));
  // This is the name of our generated test process. If the testing harness
  // changes this may need to be updated. The intent is to test for a process
  // that is running.
  EXPECT_EQ("system_monitor_harvester_test.c", test_string);
}
