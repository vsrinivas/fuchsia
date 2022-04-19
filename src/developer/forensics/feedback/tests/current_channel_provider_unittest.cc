// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/current_channel_provider.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/developer/forensics/feedback/annotations/types.h"

namespace forensics::feedback {
namespace {

using ::testing::Pair;
using ::testing::UnorderedElementsAreArray;

TEST(CurrentChannelToAnnotationsTest, Convert) {
  CurrentChannelToAnnotations convert;

  EXPECT_THAT(convert(""), UnorderedElementsAreArray({
                               Pair(kSystemUpdateChannelCurrentKey, ErrorOr<std::string>("")),
                           }));
  EXPECT_THAT(convert("channel"),
              UnorderedElementsAreArray({
                  Pair(kSystemUpdateChannelCurrentKey, ErrorOr<std::string>("channel")),
              }));
}

TEST(CurrentChannelrProvider, Keys) {
  // Safe to pass nullptrs b/c objects are never used.
  CurrentChannelProvider provider(nullptr, nullptr, nullptr);

  EXPECT_THAT(provider.GetKeys(), UnorderedElementsAreArray({
                                      kSystemUpdateChannelCurrentKey,
                                  }));
}

}  // namespace
}  // namespace forensics::feedback
