// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/reboot_info/reboot_reason.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/feedback/utils/cobalt/metrics.h"

namespace feedback {
namespace {

TEST(RebootResonTest, NotParseable) {
  EXPECT_EQ(ToCobaltRebootReason(RebootReason::kNotParseable), cobalt::RebootReason::kKernelPanic);
  EXPECT_EQ(ToCrashSignature(RebootReason::kNotParseable), "fuchsia-kernel-panic");
  EXPECT_EQ(ToCrashProgramName(RebootReason::kNotParseable), "kernel");
}

TEST(RebootResonTest, Clean) {
  EXPECT_EQ(ToCobaltRebootReason(RebootReason::kClean), cobalt::RebootReason::kClean);
}

TEST(RebootResonTest, Cold) {
  EXPECT_EQ(ToCobaltRebootReason(RebootReason::kCold), cobalt::RebootReason::kCold);
}

TEST(RebootResonTest, Spontaneous) {
  EXPECT_EQ(ToCobaltRebootReason(RebootReason::kSpontaneous), cobalt::RebootReason::kUnknown);
  EXPECT_EQ(ToCrashSignature(RebootReason::kSpontaneous), "fuchsia-reboot-unknown");
  EXPECT_EQ(ToCrashProgramName(RebootReason::kSpontaneous), "device");
}

TEST(RebootResonTest, KernelPanic) {
  EXPECT_EQ(ToCobaltRebootReason(RebootReason::kKernelPanic), cobalt::RebootReason::kKernelPanic);
  EXPECT_EQ(ToCrashSignature(RebootReason::kKernelPanic), "fuchsia-kernel-panic");
  EXPECT_EQ(ToCrashProgramName(RebootReason::kKernelPanic), "kernel");
}

TEST(RebootResonTest, OOM) {
  EXPECT_EQ(ToCobaltRebootReason(RebootReason::kOOM), cobalt::RebootReason::kOOM);
  EXPECT_EQ(ToCrashSignature(RebootReason::kOOM), "fuchsia-oom");
  EXPECT_EQ(ToCrashProgramName(RebootReason::kOOM), "system");
}

TEST(RebootResonTest, HardwareWatchdogTimeout) {
  EXPECT_EQ(ToCobaltRebootReason(RebootReason::kHardwareWatchdogTimeout),
            cobalt::RebootReason::kHardwareWatchdog);
  EXPECT_EQ(ToCrashSignature(RebootReason::kHardwareWatchdogTimeout),
            "fuchsia-hw-watchdog-timeout");
  EXPECT_EQ(ToCrashProgramName(RebootReason::kHardwareWatchdogTimeout), "device");
}

TEST(RebootResonTest, SoftwareWatchdogTimeout) {
  EXPECT_EQ(ToCobaltRebootReason(RebootReason::kSoftwareWatchdogTimeout),
            cobalt::RebootReason::kSoftwareWatchdog);
  EXPECT_EQ(ToCrashSignature(RebootReason::kSoftwareWatchdogTimeout),
            "fuchsia-sw-watchdog-timeout");
  EXPECT_EQ(ToCrashProgramName(RebootReason::kSoftwareWatchdogTimeout), "system");
}

TEST(RebootResonTest, Brownout) {
  EXPECT_EQ(ToCobaltRebootReason(RebootReason::kBrownout), cobalt::RebootReason::kBrownout);
  EXPECT_EQ(ToCrashSignature(RebootReason::kBrownout), "fuchsia-brownout");
  EXPECT_EQ(ToCrashProgramName(RebootReason::kBrownout), "device");
}

}  // namespace
}  // namespace feedback
