// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/stat.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/files/file.h"

TEST(Run, TestHermeticEnv) {
  std::string hub_name;
  files::ReadFileToString("/hub/name", &hub_name);
  // if this was not executed as component, /hub/name would be sys
  EXPECT_THAT(hub_name, testing::MatchesRegex("^env_for_test_[0-9a-f]{8}$"));
}
