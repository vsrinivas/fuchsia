// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/board_info_provider.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/developer/forensics/feedback/annotations/types.h"

namespace forensics::feedback {
namespace {

using ::testing::Pair;
using ::testing::UnorderedElementsAreArray;

TEST(BoardInfoToAnnotationsTest, Convert) {
  BoardInfoToAnnotations convert;

  fuchsia::hwinfo::BoardInfo info;
  EXPECT_THAT(convert(info), UnorderedElementsAreArray({
                                 Pair(kHardwareBoardNameKey, Error::kMissingValue),
                                 Pair(kHardwareBoardRevisionKey, Error::kMissingValue),
                             }));

  info.set_name("board_name");
  EXPECT_THAT(convert(info),
              UnorderedElementsAreArray({
                  Pair(kHardwareBoardNameKey, ErrorOr<std::string>("board_name")),
                  Pair(kHardwareBoardRevisionKey, ErrorOr<std::string>(Error::kMissingValue)),
              }));

  info.set_revision("revision");
  EXPECT_THAT(convert(info), UnorderedElementsAreArray({
                                 Pair(kHardwareBoardNameKey, ErrorOr<std::string>("board_name")),
                                 Pair(kHardwareBoardRevisionKey, ErrorOr<std::string>("revision")),
                             }));
}

TEST(BoardInforProvider, Keys) {
  // Safe to pass nullptrs b/c objects are never used.
  BoardInfoProvider provider(nullptr, nullptr, nullptr);

  EXPECT_THAT(provider.GetKeys(), UnorderedElementsAreArray({
                                      kHardwareBoardNameKey,
                                      kHardwareBoardRevisionKey,
                                  }));
}

}  // namespace
}  // namespace forensics::feedback
