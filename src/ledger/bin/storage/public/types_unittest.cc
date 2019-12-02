// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/public/types.h"

#include "gtest/gtest.h"

namespace storage {
namespace {

TEST(StorageTypeTest, GarbageCollectionPolicyBackAndForth) {
  std::vector<GarbageCollectionPolicy> policies({
      GarbageCollectionPolicy::NEVER,
      GarbageCollectionPolicy::EAGER_LIVE_REFERENCES,
      GarbageCollectionPolicy::EAGER_ROOT_NODES,
  });

  for (auto policy : policies) {
    std::string policy_string = AbslUnparseFlag(policy);
    GarbageCollectionPolicy read_policy;
    std::string error;
    ASSERT_TRUE(AbslParseFlag(policy_string, &read_policy, &error));
    ASSERT_EQ(read_policy, policy);
  }
}

TEST(StorageTypeTest, GarbageCollectionPolicyUnknownPolicy) {
  GarbageCollectionPolicy policy;
  std::string error;
  ASSERT_FALSE(AbslParseFlag("sdfsdkljsdkl", &policy, &error));
}

}  // namespace
}  // namespace storage
