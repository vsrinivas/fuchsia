// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/cpp/fidl.h>
#include <lib/async-testing/test_loop.h>
#include <lib/sys/service/cpp/service_aggregate.h>
#include <lib/sys/service/cpp/service_watcher.h>
#include <lib/sys/service/cpp/test_base.h>
#include <zircon/types.h>

#include <gmock/gmock.h>

namespace sys {
namespace {

class ServiceWatcherTest : public testing::TestBase {
 protected:
  async::TestLoop& loop() { return loop_; }

 private:
  async::TestLoop loop_;
};

TEST_F(ServiceWatcherTest, Begin) {
  auto service_aggregate = OpenServiceAggregateIn<fuchsia::examples::EchoService>(ns());
  ASSERT_TRUE(service_aggregate.is_valid());

  std::vector<std::pair<uint8_t, std::string>> instances;
  ServiceWatcher watcher([&instances](uint8_t event, std::string instance) {
    instances.emplace_back(std::make_pair(event, instance));
  });
  zx_status_t status = watcher.Begin(service_aggregate, loop().dispatcher());
  ASSERT_EQ(ZX_OK, status);

  ASSERT_TRUE(loop().RunUntilIdle());
  EXPECT_THAT(instances, ::testing::UnorderedElementsAre(
                             std::make_pair(fuchsia::io::WATCH_EVENT_EXISTING, "default"),
                             std::make_pair(fuchsia::io::WATCH_EVENT_EXISTING, "my_instance")));

  instances.clear();
  int ret = MkDir("/fuchsia.examples.EchoService/added");
  ASSERT_EQ(0, ret);
  ASSERT_TRUE(loop().RunUntilIdle());
  EXPECT_THAT(instances, ::testing::UnorderedElementsAre(
                             std::make_pair(fuchsia::io::WATCH_EVENT_ADDED, "added")));

  instances.clear();
  ret = RmDir("/fuchsia.examples.EchoService/added");
  ASSERT_EQ(0, ret);
  ASSERT_TRUE(loop().RunUntilIdle());
  EXPECT_THAT(instances, ::testing::UnorderedElementsAre(
                             std::make_pair(fuchsia::io::WATCH_EVENT_REMOVED, "added")));

  status = watcher.Cancel();
  ASSERT_EQ(ZX_OK, status);

  instances.clear();
  ret = MkDir("/fuchsia.examples.EchoService/added-after");
  ASSERT_EQ(0, ret);
  ASSERT_FALSE(loop().RunUntilIdle());
  ASSERT_TRUE(instances.empty());
}

}  // namespace
}  // namespace sys
