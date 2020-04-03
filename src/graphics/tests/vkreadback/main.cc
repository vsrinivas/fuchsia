// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/lib/fxl/test/test_settings.h"
#include "vkreadback.h"

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(Vulkan, Readback) {
  VkReadbackTest test;
  ASSERT_TRUE(test.Initialize());
  ASSERT_TRUE(test.Exec());
  ASSERT_TRUE(test.Readback());
}

TEST(Vulkan, ManyReadback) {
  std::vector<std::unique_ptr<VkReadbackTest>> tests;
  constexpr uint32_t kReps = 75;
  for (uint32_t i = 0; i < kReps; i++) {
    tests.emplace_back(std::make_unique<VkReadbackTest>());
    ASSERT_TRUE(tests.back()->Initialize());
    ASSERT_TRUE(tests.back()->Exec());
  }
  for (auto& test : tests) {
    ASSERT_TRUE(test->Readback());
  }
}
