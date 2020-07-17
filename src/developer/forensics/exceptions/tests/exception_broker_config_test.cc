// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/developer/forensics/exceptions/exception_broker.h"
#include "src/lib/fxl/test/test_settings.h"

namespace forensics {
namespace exceptions {

namespace {

constexpr char kTestConfigFile[] = "/pkg/data/enable_jitd_on_startup.json";

}  // namespace

TEST(ExceptionBrokerConfig, NonExistanceShouldNotActivate) {
  auto broker = ExceptionBroker::Create();

  ASSERT_FALSE(broker->limbo_manager().active());
}

TEST(ExceptionBrokerConfig, ExistanceShouldActivate) {
  auto broker = ExceptionBroker::Create(kTestConfigFile);

  ASSERT_TRUE(broker->limbo_manager().active());

  {
    auto& filters = broker->limbo_manager().filters();
    ASSERT_EQ(filters.size(), 0u);
  }
}

constexpr char kFilterConfigFile[] = "/pkg/data/filter_jitd_config.json";

TEST(ExceptionBrokerConfig, FilterArray) {
  auto broker = ExceptionBroker::Create(kFilterConfigFile);

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

int main(int argc, char* argv[]) {
  if (!fxl::SetTestSettings(argc, argv))
    return EXIT_FAILURE;

  testing::InitGoogleTest(&argc, argv);
  syslog::SetTags({"exception-broker", "integration-test"});

  return RUN_ALL_TESTS();
}
