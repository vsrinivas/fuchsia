// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/developer/forensics/exceptions/exception_broker.h"

namespace forensics {
namespace exceptions {

namespace {

constexpr char kTestConfigFile[] = "/pkg/data/enable_jitd_on_startup.json";

}  // namespace

TEST(ExceptionBrokerConfig, NonExistanceShouldNotActivate) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  inspect::Inspector inspector;
  auto broker =
      ExceptionBroker::Create(loop.dispatcher(), &inspector.GetRoot(), /*max_num_handlers=*/1u,
                              /*exception_ttl=*/zx::hour(1));

  ASSERT_FALSE(broker->limbo_manager().active());
}

TEST(ExceptionBrokerConfig, ExistanceShouldActivate) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  inspect::Inspector inspector;
  auto broker =
      ExceptionBroker::Create(loop.dispatcher(), &inspector.GetRoot(), /*max_num_handlers=*/
                              1u, /*exception_ttl=*/zx::hour(1), kTestConfigFile);

  ASSERT_TRUE(broker->limbo_manager().active());

  {
    auto& filters = broker->limbo_manager().filters();
    ASSERT_EQ(filters.size(), 0u);
  }
}

constexpr char kFilterConfigFile[] = "/pkg/data/filter_jitd_config.json";

TEST(ExceptionBrokerConfig, FilterArray) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  inspect::Inspector inspector;
  auto broker =
      ExceptionBroker::Create(loop.dispatcher(), &inspector.GetRoot(), /*max_num_handlers=*/1u,
                              /*exception_ttl=*/zx::hour(1), kFilterConfigFile);

  ASSERT_TRUE(broker->limbo_manager().active());

  {
    auto& filters = broker->limbo_manager().filters();
    ASSERT_EQ(filters.size(), 3u);
    auto it = filters.begin();
    EXPECT_EQ(*it++, "filter-1");
    EXPECT_EQ(*it++, "filter-2");
    EXPECT_EQ(*it++, "filter-3");
  }
}

}  // namespace exceptions
}  // namespace forensics
