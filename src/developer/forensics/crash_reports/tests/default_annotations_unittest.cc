// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/default_annotations.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics::crash_reports {
namespace {

using ::testing::Pair;
using ::testing::UnorderedElementsAreArray;

TEST(DefaultAnnotationsTest, BuildDefaultAnnotations_EmptyStartupAnnotations) {
  EXPECT_THAT(BuildDefaultAnnotations({}).Raw(),
              UnorderedElementsAreArray({
                  Pair(feedback::kOSNameKey, "Fuchsia"),
                  Pair(feedback::kOSVersionKey, "unknown"),
                  Pair("debug.osVersion.error", "missing"),
                  Pair(feedback::kBuildVersionKey, "unknown"),
                  Pair("debug.build.version.error", "missing"),
                  Pair(feedback::kBuildBoardKey, "unknown"),
                  Pair("debug.build.board.error", "missing"),
                  Pair(feedback::kBuildProductKey, "unknown"),
                  Pair("debug.build.product.error", "missing"),
                  Pair(feedback::kBuildLatestCommitDateKey, "unknown"),
                  Pair("debug.build.latest-commit-date.error", "missing"),
              }));
}

TEST(DefaultAnnotationsTest, BuildDefaultAnnotations) {
  EXPECT_THAT(
      BuildDefaultAnnotations({
                                  {feedback::kBuildVersionKey, "version"},
                                  {feedback::kBuildBoardKey, "board"},
                                  {feedback::kBuildProductKey, Error::kTimeout},
                                  {feedback::kBuildLatestCommitDateKey, Error::kFileReadFailure},
                              })
          .Raw(),
      UnorderedElementsAreArray({
          Pair(feedback::kOSNameKey, "Fuchsia"),
          Pair(feedback::kOSVersionKey, "version"),
          Pair(feedback::kBuildVersionKey, "version"),
          Pair(feedback::kBuildBoardKey, "board"),
          Pair(feedback::kBuildProductKey, "unknown"),
          Pair("debug.build.product.error", "timeout"),
          Pair(feedback::kBuildLatestCommitDateKey, "unknown"),
          Pair("debug.build.latest-commit-date.error", "file read failure"),
      }));
}

}  // namespace
}  // namespace forensics::crash_reports
