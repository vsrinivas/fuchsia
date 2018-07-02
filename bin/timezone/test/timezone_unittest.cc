// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/timezone/timezone.h"
#include "gtest/gtest.h"
#include "lib/app/cpp/testing/test_with_context.h"

namespace time_zone {
namespace test {

using namespace fuchsia::timezone;
using namespace time_zone;

constexpr char kIcuDataPath[] =
    // TODO(CP-76): use "/pkg/data/icudtl.dat"
    "/pkgfs/packages/timezone_tests/0/data/icudtl.dat";
constexpr char kTzIdPath[] =
    // TODO(CP-76): use some temp path in RAM
    "/tmp/timezone-unittest-tz_id_path";

class TimezoneUnitTest : public fuchsia::sys::testing::TestWithContext {
 protected:
  TimezoneUnitTest()
      : timezone_(std::make_unique<TimezoneImpl>(TakeContext(), kIcuDataPath,
                                                 kTzIdPath)) {}
  void TearDown() override {
    timezone_.reset();
    remove(kTzIdPath);
    TestWithContext::TearDown();
  }

  TimezonePtr timezone() {
    TimezonePtr timezone;
    controller().outgoing_public_services().ConnectToService(
        timezone.NewRequest());
    return timezone;
  }

 private:
  std::unique_ptr<TimezoneImpl> timezone_;
};

TEST_F(TimezoneUnitTest, SetTimezone_Unknown) {
  auto timezone_ptr = timezone();
  bool status = true;
  timezone_ptr->SetTimezone("invalid_timezone",
                            [&status](bool retval) { status = retval; });
  RunLoopUntilIdle();
  // Should fail
  ASSERT_FALSE(status);
}

TEST_F(TimezoneUnitTest, SetTimezone_GetTimezoneId) {
  auto timezone_ptr = timezone();
  bool success = false;
  fidl::StringPtr expected_timezone = "America/Los_Angeles";
  timezone_ptr->SetTimezone(expected_timezone,
                            [&success](bool retval) { success = retval; });
  RunLoopUntilIdle();
  ASSERT_TRUE(success);

  fidl::StringPtr actual_timezone = "bogus";
  timezone_ptr->GetTimezoneId(
      [&actual_timezone](fidl::StringPtr retval) { actual_timezone = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(expected_timezone, actual_timezone);
}

TEST_F(TimezoneUnitTest, SetTimezone_GetTimezoneOffsetMinutes) {
  auto timezone_ptr = timezone();
  bool success = false;
  timezone_ptr->SetTimezone("America/Los_Angeles",
                            [&success](bool retval) { success = retval; });
  RunLoopUntilIdle();
  ASSERT_TRUE(success);
  // No sense in proceeding if SetTimezone failed because expectations below
  // should fail in this case.

  int32_t local_offset = INT32_MAX;
  int32_t dst_offset = INT32_MAX;
  int64_t milliseconds_since_epoch = 12345;
  timezone_ptr->GetTimezoneOffsetMinutes(
      milliseconds_since_epoch,
      [&local_offset, &dst_offset](int32_t local, int32_t dst) {
        local_offset = local;
        dst_offset = dst;
      });
  RunLoopUntilIdle();
  EXPECT_EQ(local_offset, -480);
  EXPECT_EQ(dst_offset, 0);

  // Test that we can change the timezone after it's already been set once
  success = false;
  timezone_ptr->SetTimezone("Israel",
                            [&success](bool retval) { success = retval; });
  RunLoopUntilIdle();
  ASSERT_TRUE(success);

  timezone_ptr->GetTimezoneOffsetMinutes(
      milliseconds_since_epoch,
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
  void OnTimezoneOffsetChange(fidl::StringPtr timezone_id) {
    last_seen_timezone = timezone_id;
  }
  fidl::StringPtr last_seen_timezone;
  void AddBinding(fidl::InterfaceRequest<TimezoneWatcher> request) {
    bindings_.AddBinding(this, std::move(request));
  }

 private:
  fidl::BindingSet<TimezoneWatcher> bindings_;
};

TEST_F(TimezoneUnitTest, SetTimezone_Watcher) {
  TimezoneWatcherForTest watcher;
  TimezoneWatcherPtr watcher_ptr;
  watcher.AddBinding(watcher_ptr.NewRequest());

  auto timezone_ptr = timezone();
  timezone_ptr->Watch(watcher_ptr.Unbind());
  RunLoopUntilIdle();
  fidl::StringPtr expected_timezone = "America/Los_Angeles";
  ASSERT_NE(expected_timezone, watcher.last_seen_timezone);

  bool success = false;
  timezone_ptr->SetTimezone(expected_timezone,
                            [&success](bool retval) { success = retval; });
  RunLoopUntilIdle();
  ASSERT_TRUE(success);

  ASSERT_EQ(expected_timezone, watcher.last_seen_timezone);
}

}  // namespace test
}  // namespace time_zone
