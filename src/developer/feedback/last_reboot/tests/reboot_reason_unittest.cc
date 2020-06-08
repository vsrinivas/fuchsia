// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/last_reboot/reboot_reason.h"

#include <fuchsia/feedback/cpp/fidl.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/feedback/utils/cobalt/metrics.h"

namespace feedback {
namespace {

TEST(RebootResonTest, NotParseable) {
  EXPECT_EQ(ToCobaltLegacyRebootReason(RebootReason::kNotParseable),
            cobalt::LegacyRebootReason::kKernelPanic);
  EXPECT_EQ(ToCobaltLastRebootReason(RebootReason::kNotParseable),
            cobalt::LastRebootReason::kUnknown);
  EXPECT_EQ(ToCrashSignature(RebootReason::kNotParseable), "fuchsia-kernel-panic");
  EXPECT_EQ(ToCrashProgramName(RebootReason::kNotParseable), "kernel");
  EXPECT_EQ(ToFidlRebootReason(RebootReason::kNotParseable), std::nullopt);
}

TEST(RebootResonTest, Clean) {
  EXPECT_EQ(ToCobaltLegacyRebootReason(RebootReason::kGenericGraceful),
            cobalt::LegacyRebootReason::kClean);
  EXPECT_EQ(ToCobaltLastRebootReason(RebootReason::kGenericGraceful),
            cobalt::LastRebootReason::kGenericGraceful);
  EXPECT_EQ(ToFidlRebootReason(RebootReason::kGenericGraceful), std::nullopt);
}

TEST(RebootResonTest, Cold) {
  EXPECT_EQ(ToCobaltLegacyRebootReason(RebootReason::kCold), cobalt::LegacyRebootReason::kCold);
  EXPECT_EQ(ToCobaltLastRebootReason(RebootReason::kCold), cobalt::LastRebootReason::kCold);
  EXPECT_EQ(ToFidlRebootReason(RebootReason::kCold), fuchsia::feedback::RebootReason::COLD);
}

TEST(RebootResonTest, Spontaneous) {
  EXPECT_EQ(ToCobaltLegacyRebootReason(RebootReason::kSpontaneous),
            cobalt::LegacyRebootReason::kUnknown);
  EXPECT_EQ(ToCobaltLastRebootReason(RebootReason::kSpontaneous),
            cobalt::LastRebootReason::kBriefPowerLoss);
  EXPECT_EQ(ToCrashSignature(RebootReason::kSpontaneous), "fuchsia-brief-power-loss");
  EXPECT_EQ(ToCrashProgramName(RebootReason::kSpontaneous), "device");
  EXPECT_EQ(ToFidlRebootReason(RebootReason::kSpontaneous),
            fuchsia::feedback::RebootReason::BRIEF_POWER_LOSS);
}

TEST(RebootResonTest, KernelPanic) {
  EXPECT_EQ(ToCobaltLegacyRebootReason(RebootReason::kKernelPanic),
            cobalt::LegacyRebootReason::kKernelPanic);
  EXPECT_EQ(ToCobaltLastRebootReason(RebootReason::kKernelPanic),
            cobalt::LastRebootReason::kKernelPanic);
  EXPECT_EQ(ToCrashSignature(RebootReason::kKernelPanic), "fuchsia-kernel-panic");
  EXPECT_EQ(ToCrashProgramName(RebootReason::kKernelPanic), "kernel");
  EXPECT_EQ(ToFidlRebootReason(RebootReason::kKernelPanic),
            fuchsia::feedback::RebootReason::KERNEL_PANIC);
}

TEST(RebootResonTest, OOM) {
  EXPECT_EQ(ToCobaltLegacyRebootReason(RebootReason::kOOM), cobalt::LegacyRebootReason::kOOM);
  EXPECT_EQ(ToCobaltLastRebootReason(RebootReason::kOOM),
            cobalt::LastRebootReason::kSystemOutOfMemory);
  EXPECT_EQ(ToCrashSignature(RebootReason::kOOM), "fuchsia-oom");
  EXPECT_EQ(ToCrashProgramName(RebootReason::kOOM), "system");
  EXPECT_EQ(ToFidlRebootReason(RebootReason::kOOM),
            fuchsia::feedback::RebootReason::SYSTEM_OUT_OF_MEMORY);
}

TEST(RebootResonTest, HardwareWatchdogTimeout) {
  EXPECT_EQ(ToCobaltLegacyRebootReason(RebootReason::kHardwareWatchdogTimeout),
            cobalt::LegacyRebootReason::kHardwareWatchdog);
  EXPECT_EQ(ToCobaltLastRebootReason(RebootReason::kHardwareWatchdogTimeout),
            cobalt::LastRebootReason::kHardwareWatchdogTimeout);
  EXPECT_EQ(ToCrashSignature(RebootReason::kHardwareWatchdogTimeout),
            "fuchsia-hw-watchdog-timeout");
  EXPECT_EQ(ToCrashProgramName(RebootReason::kHardwareWatchdogTimeout), "device");
  EXPECT_EQ(ToFidlRebootReason(RebootReason::kHardwareWatchdogTimeout),
            fuchsia::feedback::RebootReason::HARDWARE_WATCHDOG_TIMEOUT);
}

TEST(RebootResonTest, SoftwareWatchdogTimeout) {
  EXPECT_EQ(ToCobaltLegacyRebootReason(RebootReason::kSoftwareWatchdogTimeout),
            cobalt::LegacyRebootReason::kSoftwareWatchdog);
  EXPECT_EQ(ToCobaltLastRebootReason(RebootReason::kSoftwareWatchdogTimeout),
            cobalt::LastRebootReason::kSoftwareWatchdogTimeout);
  EXPECT_EQ(ToCrashSignature(RebootReason::kSoftwareWatchdogTimeout),
            "fuchsia-sw-watchdog-timeout");
  EXPECT_EQ(ToCrashProgramName(RebootReason::kSoftwareWatchdogTimeout), "system");
  EXPECT_EQ(ToFidlRebootReason(RebootReason::kSoftwareWatchdogTimeout),
            fuchsia::feedback::RebootReason::SOFTWARE_WATCHDOG_TIMEOUT);
}

TEST(RebootResonTest, Brownout) {
  EXPECT_EQ(ToCobaltLegacyRebootReason(RebootReason::kBrownout),
            cobalt::LegacyRebootReason::kBrownout);
  EXPECT_EQ(ToCobaltLastRebootReason(RebootReason::kBrownout), cobalt::LastRebootReason::kBrownout);
  EXPECT_EQ(ToCrashSignature(RebootReason::kBrownout), "fuchsia-brownout");
  EXPECT_EQ(ToCrashProgramName(RebootReason::kBrownout), "device");
  EXPECT_EQ(ToFidlRebootReason(RebootReason::kBrownout), fuchsia::feedback::RebootReason::BROWNOUT);
}

}  // namespace
}  // namespace feedback
