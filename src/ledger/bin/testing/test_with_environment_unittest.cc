// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/test_with_environment.h"

#include <gtest/gtest.h>

#include "src/ledger/bin/app/flags.h"
#include "src/ledger/bin/storage/public/types.h"

namespace ledger {
namespace {

using TestWithEnvironmentTest = TestWithEnvironment;

TEST_F(TestWithEnvironmentTest, TestingGcPolicy) {
  EXPECT_EQ(environment_.gc_policy(), kTestingGarbageCollectionPolicy);
}

class TestWithModifiedEnvironmentTest : public TestWithEnvironment {
 public:
  TestWithModifiedEnvironmentTest()
      : TestWithEnvironment([](EnvironmentBuilder* builder) {
          builder->SetGcPolicy(storage::GarbageCollectionPolicy::NEVER);
        }) {}
};

TEST_F(TestWithModifiedEnvironmentTest, GcPolicy) {
  EXPECT_EQ(environment_.gc_policy(), storage::GarbageCollectionPolicy::NEVER);
}

}  // namespace
}  // namespace ledger
