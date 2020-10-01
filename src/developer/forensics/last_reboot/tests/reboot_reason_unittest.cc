// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/last_reboot/reboot_reason.h"

#include <fuchsia/feedback/cpp/fidl.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/utils/cobalt/metrics.h"

namespace forensics {
namespace last_reboot {
namespace {

TEST(RebootReasonTest, NotParseable) {
  const auto reason = RebootReason::kNotParseable;

  EXPECT_TRUE(IsCrash(reason));
  EXPECT_EQ(ToCobaltLastRebootReason(reason), cobalt::LastRebootReason::kUnknown);
  EXPECT_EQ(ToCrashSignature(reason), "fuchsia-reboot-log-not-parseable");
  EXPECT_EQ(ToCrashProgramName(reason), "reboot-log");
  EXPECT_EQ(ToFidlRebootReason(reason), std::nullopt);
}

TEST(RebootReasonTest, Cold) {
  const auto reason = RebootReason::kCold;

  EXPECT_FALSE(IsCrash(reason));
  EXPECT_EQ(ToCobaltLastRebootReason(reason), cobalt::LastRebootReason::kCold);
  EXPECT_EQ(ToFidlRebootReason(reason), fuchsia::feedback::RebootReason::COLD);
}

TEST(RebootReasonTest, Spontaneous) {
  const auto reason = RebootReason::kSpontaneous;

  EXPECT_TRUE(IsCrash(reason));
  EXPECT_EQ(ToCobaltLastRebootReason(reason), cobalt::LastRebootReason::kBriefPowerLoss);
  EXPECT_EQ(ToCrashSignature(reason), "fuchsia-brief-power-loss");
  EXPECT_EQ(ToCrashProgramName(reason), "device");
  EXPECT_EQ(ToFidlRebootReason(reason), fuchsia::feedback::RebootReason::BRIEF_POWER_LOSS);
}

TEST(RebootReasonTest, KernelPanic) {
  const auto reason = RebootReason::kKernelPanic;

  EXPECT_TRUE(IsCrash(reason));
  EXPECT_EQ(ToCobaltLastRebootReason(reason), cobalt::LastRebootReason::kKernelPanic);
  EXPECT_EQ(ToCrashSignature(reason), "fuchsia-kernel-panic");
  EXPECT_EQ(ToCrashProgramName(reason), "kernel");
  EXPECT_EQ(ToFidlRebootReason(reason), fuchsia::feedback::RebootReason::KERNEL_PANIC);
}

TEST(RebootReasonTest, OOM) {
  const auto reason = RebootReason::kOOM;

  EXPECT_TRUE(IsCrash(reason));
  EXPECT_EQ(ToCobaltLastRebootReason(reason), cobalt::LastRebootReason::kSystemOutOfMemory);
  EXPECT_EQ(ToCrashSignature(reason), "fuchsia-oom");
  EXPECT_EQ(ToCrashProgramName(reason), "system");
  EXPECT_EQ(ToFidlRebootReason(reason), fuchsia::feedback::RebootReason::SYSTEM_OUT_OF_MEMORY);
}

TEST(RebootReasonTest, HardwareWatchdogTimeout) {
  const auto reason = RebootReason::kHardwareWatchdogTimeout;

  EXPECT_TRUE(IsCrash(reason));
  EXPECT_EQ(ToCobaltLastRebootReason(reason), cobalt::LastRebootReason::kHardwareWatchdogTimeout);
  EXPECT_EQ(ToCrashSignature(reason), "fuchsia-hw-watchdog-timeout");
  EXPECT_EQ(ToCrashProgramName(reason), "device");
  EXPECT_EQ(ToFidlRebootReason(reason), fuchsia::feedback::RebootReason::HARDWARE_WATCHDOG_TIMEOUT);
}

TEST(RebootReasonTest, SoftwareWatchdogTimeout) {
  const auto reason = RebootReason::kSoftwareWatchdogTimeout;

  EXPECT_TRUE(IsCrash(reason));
  EXPECT_EQ(ToCobaltLastRebootReason(reason), cobalt::LastRebootReason::kSoftwareWatchdogTimeout);
  EXPECT_EQ(ToCrashSignature(reason), "fuchsia-sw-watchdog-timeout");
  EXPECT_EQ(ToCrashProgramName(reason), "system");
  EXPECT_EQ(ToFidlRebootReason(reason), fuchsia::feedback::RebootReason::SOFTWARE_WATCHDOG_TIMEOUT);
}

TEST(RebootReasonTest, Brownout) {
  const auto reason = RebootReason::kBrownout;

  EXPECT_TRUE(IsCrash(reason));
  EXPECT_EQ(ToCobaltLastRebootReason(reason), cobalt::LastRebootReason::kBrownout);
  EXPECT_EQ(ToCrashSignature(reason), "fuchsia-brownout");
  EXPECT_EQ(ToCrashProgramName(reason), "device");
  EXPECT_EQ(ToFidlRebootReason(reason), fuchsia::feedback::RebootReason::BROWNOUT);
}

TEST(RebootReasonTest, GenericGraceful) {
  const auto reason = RebootReason::kGenericGraceful;

  EXPECT_FALSE(IsCrash(reason));
  EXPECT_EQ(ToCobaltLastRebootReason(reason), cobalt::LastRebootReason::kGenericGraceful);
  EXPECT_EQ(ToFidlRebootReason(reason), std::nullopt);
}

TEST(RebootReasonTest, UserRequest) {
  const auto reason = RebootReason::kUserRequest;

  EXPECT_FALSE(IsCrash(reason));
  EXPECT_EQ(ToCobaltLastRebootReason(reason), cobalt::LastRebootReason::kUserRequest);
  EXPECT_EQ(ToFidlRebootReason(reason), fuchsia::feedback::RebootReason::USER_REQUEST);
}

TEST(RebootReasonTest, SystemUpdate) {
  const auto reason = RebootReason::kSystemUpdate;

  EXPECT_FALSE(IsCrash(reason));
  EXPECT_EQ(ToCobaltLastRebootReason(reason), cobalt::LastRebootReason::kSystemUpdate);
  EXPECT_EQ(ToFidlRebootReason(reason), fuchsia::feedback::RebootReason::SYSTEM_UPDATE);
}

TEST(RebootReasonTest, HighTemperature) {
  const auto reason = RebootReason::kHighTemperature;

  EXPECT_FALSE(IsCrash(reason));
  EXPECT_EQ(ToCobaltLastRebootReason(reason), cobalt::LastRebootReason::kHighTemperature);
  EXPECT_EQ(ToFidlRebootReason(reason), fuchsia::feedback::RebootReason::HIGH_TEMPERATURE);
}

TEST(RebootReasonTest, SessionFailure) {
  const auto reason = RebootReason::kSessionFailure;

  EXPECT_TRUE(IsCrash(reason));
  EXPECT_EQ(ToCobaltLastRebootReason(reason), cobalt::LastRebootReason::kSessionFailure);
  EXPECT_EQ(ToCrashSignature(reason), "fuchsia-session-failure");
  EXPECT_EQ(ToCrashProgramName(reason), "system");
  EXPECT_EQ(ToFidlRebootReason(reason), fuchsia::feedback::RebootReason::SESSION_FAILURE);
}

TEST(RebootReasonTest, SystemFailure) {
  const auto reason = RebootReason::kSystemFailure;

  EXPECT_TRUE(IsCrash(reason));
  EXPECT_EQ(ToCobaltLastRebootReason(reason), cobalt::LastRebootReason::kSystemFailure);
  EXPECT_EQ(ToCrashSignature(reason), "fuchsia-system-failure");
  EXPECT_EQ(ToCrashProgramName(reason), "system");
  EXPECT_EQ(ToFidlRebootReason(reason), fuchsia::feedback::RebootReason::SYSTEM_FAILURE);
}

TEST(RebootReasonTest, FDR) {
  const auto reason = RebootReason::kFdr;

  EXPECT_FALSE(IsCrash(reason));
  EXPECT_EQ(ToCobaltLastRebootReason(reason), cobalt::LastRebootReason::kFactoryDataReset);
  EXPECT_EQ(ToFidlRebootReason(reason), fuchsia::feedback::RebootReason::FACTORY_DATA_RESET);
}

}  // namespace
}  // namespace last_reboot
}  // namespace forensics
