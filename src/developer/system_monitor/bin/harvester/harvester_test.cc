// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "harvester.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async-testing/dispatcher_stub.h>

#include <gtest/gtest.h>

#include "dockyard_proxy_fake.h"
#include "root_resource.h"

namespace {

class AsyncDispatcherFake : public async::DispatcherStub {
 public:
  zx::time Now() override { return current_time_; }
  void SetTime(zx::time t) { current_time_ = t; }

 private:
  zx::time current_time_;
};

}  // namespace

class SystemMonitorHarvesterTest : public ::testing::Test {
 public:
  void SetUp() override {
    // Create a test harvester.
    std::unique_ptr<harvester::DockyardProxyFake> dockyard_proxy =
        std::make_unique<harvester::DockyardProxyFake>();

    EXPECT_EQ(harvester::GetRootResource(&root_resource), ZX_OK);
    test_harvester = std::make_unique<harvester::Harvester>(
        root_resource, std::move(dockyard_proxy));
  }

  zx_handle_t GetHarvesterRootResource() const {
    return test_harvester->root_resource_;
  }
  zx::duration GetGatherThreadsAndCpuPeriod() const {
    return test_harvester->gather_threads_and_cpu_.update_period_;
  }
  zx::duration GetGatherInspectablePeriod() const {
    return test_harvester->gather_inspectable_.update_period_;
  }
  zx::duration GetGatherIntrospectionPeriod() const {
    return test_harvester->gather_introspection_.update_period_;
  }
  zx::duration GetGatherMemoryPeriod() const {
    return test_harvester->gather_memory_.update_period_;
  }
  zx::duration GetGatherProcessesAndMemoryPeriod() const {
    return test_harvester->gather_processes_and_memory_.update_period_;
  }

  std::unique_ptr<harvester::Harvester> test_harvester;
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  zx_handle_t root_resource;
};

TEST_F(SystemMonitorHarvesterTest, CreateHarvester) {
  AsyncDispatcherFake fast_dispatcher;
  AsyncDispatcherFake slow_dispatcher;
  EXPECT_EQ(root_resource, GetHarvesterRootResource());

  test_harvester->GatherFastData(&fast_dispatcher);
  EXPECT_EQ(zx::msec(100), GetGatherThreadsAndCpuPeriod());

  test_harvester->GatherSlowData(&slow_dispatcher);
  // TODO(fxbug.dev/40872): re-enable once we need this data.
  // EXPECT_EQ(zx::sec(3), GetGatherInspectablePeriod());
  // EXPECT_EQ(zx::sec(10), GetGatherIntrospectionPeriod());
  EXPECT_EQ(zx::sec(2), GetGatherProcessesAndMemoryPeriod());
}
