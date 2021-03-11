// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/metrics/config.h"

#include <zircon/syscalls/object.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/memory/metrics/tests/test_utils.h"
#include "zircon/system/public/zircon/types.h"

using testing::SizeIs;

namespace memory {
namespace test {

using ConfigUnitTest = testing::Test;

const std::string kValidConfiguration = R"([
    {
        "name": "ContiguousPool",
        "process": "driver_host:.*",
        "vmo": "SysmemContiguousPool",
        "event_code": 1
    },
    {
        "name": "Blobfs",
        "process": ".*blobfs",
        "vmo": ".*",
        "event_code": 2
    }
])";

Vmo CreateVmo(const std::string& name, size_t num_children = 0) {
  zx_info_vmo_t vmo_info{0, {}, 0, 0, num_children, 0, 0, 0, {0, 0, 0, 0}, 0, 0, 0, 0, 0};
  name.copy(vmo_info.name, ZX_MAX_NAME_LEN);
  return Vmo(vmo_info);
}

TEST_F(ConfigUnitTest, ValidConfiguration) {
  std::vector<BucketMatch> bucket_matches;
  EXPECT_TRUE(BucketMatch::ReadBucketMatchesFromConfig(kValidConfiguration, &bucket_matches));

  EXPECT_THAT(bucket_matches, SizeIs(2));

  BucketMatch& match_0 = bucket_matches[0];
  EXPECT_EQ(match_0.name(), "ContiguousPool");
  EXPECT_EQ(match_0.event_code(), 1);
  EXPECT_TRUE(match_0.ProcessMatch("driver_host:some_process"));
  EXPECT_TRUE(match_0.VmoMatch(CreateVmo("SysmemContiguousPool")));

  BucketMatch& match_1 = bucket_matches[1];
  EXPECT_EQ(match_1.name(), "Blobfs");
  EXPECT_EQ(match_1.event_code(), 2);
  EXPECT_TRUE(match_1.ProcessMatch("active_blobfs"));
  EXPECT_TRUE(match_1.VmoMatch(CreateVmo("blob-01234", 4)));
}

TEST_F(ConfigUnitTest, InvalidConfiguration) {
  std::vector<BucketMatch> bucket_matches;
  // Missing "name"
  EXPECT_FALSE(BucketMatch::ReadBucketMatchesFromConfig(R"([{"process": "a", "vmo": ".*"}])",
                                                        &bucket_matches));
  // Missing "process"
  EXPECT_FALSE(
      BucketMatch::ReadBucketMatchesFromConfig(R"([{"name": "a", "vmo": ".*"}])", &bucket_matches));
  // Missing "vmo"
  EXPECT_FALSE(BucketMatch::ReadBucketMatchesFromConfig(R"([{"name": "a", "process": ".*"}])",
                                                        &bucket_matches));

  // Badly formatted JSON
  EXPECT_FALSE(BucketMatch::ReadBucketMatchesFromConfig(
      R"([{"name": "a", "process": ".*", "vmo": ".*"]})", &bucket_matches));
}

}  // namespace test
}  // namespace memory
