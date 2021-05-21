// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/reboot_log/graceful_reboot_reason.h"

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace forensics {
namespace feedback {
namespace {

TEST(GracefulRebootReasonTest, VerifyContentConversion) {
  // ToFileContent() & FromFileContent() for reboot reasons from |power::statecontrol::RebootReason|
  // should be reversible.

  const std::vector<GracefulRebootReason> reasons = {
      GracefulRebootReason::kUserRequest,
      GracefulRebootReason::kSystemUpdate,
      GracefulRebootReason::kRetrySystemUpdate,
      GracefulRebootReason::kHighTemperature,
      GracefulRebootReason::kSessionFailure,
      GracefulRebootReason::kSysmgrFailure,
      GracefulRebootReason::kCriticalComponentFailure,
      GracefulRebootReason::kFdr,
      GracefulRebootReason::kZbiSwap,
      GracefulRebootReason::kNotSupported,
  };

  for (const auto reason : reasons) {
    EXPECT_EQ((int)reason, (int)FromFileContent(ToFileContent(reason)));
  }
}

}  // namespace
}  // namespace feedback
}  // namespace forensics
