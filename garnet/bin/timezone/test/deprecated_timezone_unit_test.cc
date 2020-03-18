// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/test_loop_fixture.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "garnet/bin/timezone/timezone.h"
#include "gtest/gtest.h"
#include "lib/sys/inspect/cpp/component.h"
#include "zircon/system/ulib/inspect/include/lib/inspect/cpp/hierarchy.h"
#include "zircon/system/ulib/inspect/include/lib/inspect/cpp/reader.h"

namespace time_zone {
namespace test {

using namespace fuchsia::deprecatedtimezone;
using namespace time_zone;

constexpr char kIcuDataPath[] = "/pkg/data/icudtl.dat";
constexpr char kTzIdPath[] = "/tmp/timezone-unittest-tz_id_path";

class DeprecatedTimeZoneUnitTest : public gtest::TestLoopFixture {
 protected:
  DeprecatedTimeZoneUnitTest()
      : timezone_(std::make_unique<TimezoneImpl>(context_provider_.TakeContext(), kIcuDataPath,
                                                 kTzIdPath)) {}
  void TearDown() override {
    timezone_.reset();
    remove(kTzIdPath);
    TestLoopFixture::TearDown();
  }

  TimezonePtr timezone() {
    TimezonePtr timezone;
    context_provider_.ConnectToPublicService(timezone.NewRequest());
    return timezone;
  }

  std::string GetHealth(const inspect::Hierarchy& hierarchy) {
    auto* node = hierarchy.GetByPath({"fuchsia.inspect.Health"});
    if (node == nullptr) {
      return "";
    }

    auto* val = node->node().get_property<inspect::StringPropertyValue>("status");
    return val ? val->value() : "";
  }

  std::string GetTZ(const inspect::Hierarchy& hierarchy) {
    auto* val = hierarchy.node().get_property<inspect::StringPropertyValue>("timezone");
    return val ? val->value() : "";
  }

 private:
  sys::testing::ComponentContextProvider context_provider_;

 protected:
  std::unique_ptr<TimezoneImpl> timezone_;
};

TEST_F(DeprecatedTimeZoneUnitTest, SetTimezone_Unknown) {
  auto timezone_ptr = timezone();
  bool status = true;
  timezone_ptr->SetTimezone("invalid_timezone", [&status](bool retval) { status = retval; });
  RunLoopUntilIdle();

  // Should fail
  ASSERT_FALSE(status);

  auto hierarchy = inspect::ReadFromVmo(timezone_->inspector().DuplicateVmo()).take_value();
  EXPECT_EQ("OK", GetHealth(hierarchy));
}

TEST_F(DeprecatedTimeZoneUnitTest, SetTimezone_GetTimezoneId) {
  auto timezone_ptr = timezone();
  bool success = false;
  std::string expected_timezone = "America/Los_Angeles";
  timezone_ptr->SetTimezone(expected_timezone, [&success](bool retval) { success = retval; });
  RunLoopUntilIdle();
  ASSERT_TRUE(success);

  auto hierarchy = inspect::ReadFromVmo(timezone_->inspector().DuplicateVmo()).take_value();
  EXPECT_EQ("OK", GetHealth(hierarchy));
  EXPECT_EQ("America/Los_Angeles", GetTZ(hierarchy));

  fidl::StringPtr actual_timezone = "bogus";
  timezone_ptr->GetTimezoneId(
      [&actual_timezone](fidl::StringPtr retval) { actual_timezone = retval; });
  RunLoopUntilIdle();
  ASSERT_TRUE(actual_timezone.has_value());
  ASSERT_EQ(expected_timezone, actual_timezone.value());
  hierarchy = inspect::ReadFromVmo(timezone_->inspector().DuplicateVmo()).take_value();
  EXPECT_EQ("OK", GetHealth(hierarchy));
  EXPECT_EQ("America/Los_Angeles", GetTZ(hierarchy));
}

TEST_F(DeprecatedTimeZoneUnitTest, SetTimezone_GetTimezoneOffsetMinutes) {
  auto timezone_ptr = timezone();
  bool success = false;
  timezone_ptr->SetTimezone("America/Los_Angeles", [&success](bool retval) { success = retval; });
  RunLoopUntilIdle();
  ASSERT_TRUE(success);
  // No sense in proceeding if SetTimezone failed because expectations below
  // should fail in this case.

  int32_t local_offset = INT32_MAX;
  int32_t dst_offset = INT32_MAX;
  int64_t milliseconds_since_epoch = 12345;
  timezone_ptr->GetTimezoneOffsetMinutes(milliseconds_since_epoch,
                                         [&local_offset, &dst_offset](int32_t local, int32_t dst) {
                                           local_offset = local;
                                           dst_offset = dst;
                                         });
  RunLoopUntilIdle();
  EXPECT_EQ(local_offset, -480);
  EXPECT_EQ(dst_offset, 0);

  // Test that we can change the timezone after it's already been set once
  success = false;
  timezone_ptr->SetTimezone("Israel", [&success](bool retval) { success = retval; });
  RunLoopUntilIdle();
  ASSERT_TRUE(success);

  timezone_ptr->GetTimezoneOffsetMinutes(milliseconds_since_epoch,
                                         [&local_offset, &dst_offset](int32_t local, int32_t dst) {
                                           local_offset = local;
                                           dst_offset = dst;
                                         });
  RunLoopUntilIdle();
  EXPECT_EQ(local_offset, 120);
  EXPECT_EQ(dst_offset, 0);
}

class TimezoneWatcherForTest : TimezoneWatcher {
 public:
  void OnTimezoneOffsetChange(std::string timezone_id) override {
    last_seen_timezone = timezone_id;
  }
  std::string last_seen_timezone;
  void AddBinding(fidl::InterfaceRequest<TimezoneWatcher> request) {
    bindings_.AddBinding(this, std::move(request));
  }

 private:
  fidl::BindingSet<TimezoneWatcher> bindings_;
};

TEST_F(DeprecatedTimeZoneUnitTest, SetTimezone_Watcher) {
  TimezoneWatcherForTest watcher;
  TimezoneWatcherPtr watcher_ptr;
  watcher.AddBinding(watcher_ptr.NewRequest());

  auto timezone_ptr = timezone();
  timezone_ptr->Watch(watcher_ptr.Unbind());
  RunLoopUntilIdle();
  std::string expected_timezone = "America/Los_Angeles";
  ASSERT_NE(expected_timezone, watcher.last_seen_timezone);

  bool success = false;
  timezone_ptr->SetTimezone(expected_timezone, [&success](bool retval) { success = retval; });
  RunLoopUntilIdle();
  ASSERT_TRUE(success);

  ASSERT_EQ(expected_timezone, watcher.last_seen_timezone);
}

}  // namespace test
}  // namespace time_zone
