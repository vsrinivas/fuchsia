// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/last_reboot/last_reboot_info_provider.h"

#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/reboot_log/reboot_log.h"
#include "src/developer/forensics/feedback/reboot_log/reboot_reason.h"
#include "src/developer/forensics/last_reboot/last_reboot_info_provider.h"
#include "src/developer/forensics/testing/gpretty_printers.h"

namespace forensics {
namespace last_reboot {
namespace {

fuchsia::feedback::LastReboot GetLastReboot(
    const feedback::RebootReason reboot_reason,
    const std::optional<zx::duration> uptime = std::nullopt) {
  const feedback::RebootLog reboot_log(reboot_reason, "", uptime, std::nullopt);

  fuchsia::feedback::LastReboot out_last_reboot;

  LastRebootInfoProvider last_reboot_info_provider(reboot_log);
  last_reboot_info_provider.Get(
      [&](fuchsia::feedback::LastReboot last_reboot) { out_last_reboot = std::move(last_reboot); });

  return out_last_reboot;
}

TEST(LastRebootInfoProviderTest, Succeed_Graceful) {
  const feedback::RebootReason reboot_reason = feedback::RebootReason::kGenericGraceful;

  const auto last_reboot = GetLastReboot(reboot_reason);

  ASSERT_TRUE(last_reboot.has_graceful());
  EXPECT_TRUE(last_reboot.graceful());

  EXPECT_FALSE(last_reboot.has_reason());
}

TEST(LastRebootInfoProviderTest, Succeed_NotGraceful) {
  const feedback::RebootReason reboot_reason = feedback::RebootReason::kKernelPanic;

  const auto last_reboot = GetLastReboot(reboot_reason);

  ASSERT_TRUE(last_reboot.has_graceful());
  EXPECT_FALSE(last_reboot.graceful());

  ASSERT_TRUE(last_reboot.has_reason());
  EXPECT_EQ(last_reboot.reason(), ToFidlRebootReason(reboot_reason));
}

TEST(LastRebootInfoProviderTest, Succeed_HasUptime) {
  const zx::duration uptime = zx::msec(100);

  const auto last_reboot = GetLastReboot(feedback::RebootReason::kGenericGraceful, uptime);

  ASSERT_TRUE(last_reboot.has_uptime());
  EXPECT_EQ(last_reboot.uptime(), uptime.to_nsecs());
}

TEST(LastRebootInfoProviderTest, Succeed_DoesNotHaveUptime) {
  const auto last_reboot = GetLastReboot(feedback::RebootReason::kGenericGraceful, std::nullopt);

  EXPECT_FALSE(last_reboot.has_uptime());
}

TEST(LastRebootInfoProviderTest, Succeed_NotParseable) {
  const feedback::RebootReason reboot_reason = feedback::RebootReason::kNotParseable;

  const auto last_reboot = GetLastReboot(reboot_reason);

  EXPECT_FALSE(last_reboot.has_graceful());
  EXPECT_FALSE(last_reboot.has_reason());
}

}  // namespace
}  // namespace last_reboot
}  // namespace forensics
