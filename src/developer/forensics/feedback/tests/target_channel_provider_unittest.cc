// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/target_channel_provider.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"

namespace forensics::feedback {
namespace {

using ::testing::Pair;
using ::testing::UnorderedElementsAreArray;

TEST(TargetChannelToAnnotationsTest, Convert) {
  TargetChannelToAnnotations convert;

  EXPECT_THAT(convert(""), UnorderedElementsAreArray({
                               Pair(kSystemUpdateChannelTargetKey, ErrorOr<std::string>("")),
                           }));
  EXPECT_THAT(convert("channel"),
              UnorderedElementsAreArray({
                  Pair(kSystemUpdateChannelTargetKey, ErrorOr<std::string>("channel")),
              }));
  EXPECT_THAT(convert(Error::kConnectionError),
              UnorderedElementsAreArray({
                  Pair(kSystemUpdateChannelTargetKey, Error::kConnectionError),
              }));
}

using TargetChannelProviderTest = UnitTestFixture;

TEST_F(TargetChannelProviderTest, Keys) {
  // Safe to pass nullptrs b/c objects are never used.
  TargetChannelProvider provider(dispatcher(), services(), nullptr);

  EXPECT_THAT(provider.GetKeys(), UnorderedElementsAreArray({
                                      kSystemUpdateChannelTargetKey,
                                  }));
}

}  // namespace
}  // namespace forensics::feedback
