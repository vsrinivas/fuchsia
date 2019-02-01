// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_TEST_GTEST_VULKAN_H_
#define LIB_ESCHER_TEST_GTEST_VULKAN_H_

#include "garnet/public/lib/escher/test/gtest_vulkan_internal.h"
#include "garnet/public/lib/escher/util/check_vulkan_support.h"

// Tests declared using this macro are only registered with GTest when Vulkan
// is available.
#define VK_TEST(test_case_name, test_name)                   \
  VK_GTEST_TEST_(test_case_name, test_name, ::testing::Test, \
                 ::testing::internal::GetTestTypeId())

// Tests declared using this macro are only registered with GTest when Vulkan
// is available.
#define VK_TEST_F(test_fixture, test_name)              \
  VK_GTEST_TEST_(test_fixture, test_name, test_fixture, \
                 ::testing::internal::GetTypeId<test_fixture>())

// Tests that require Vulkan are suppressed if Vulkan is not supported.
#define VK_TESTS_SUPPRESSED() (!escher::VulkanIsSupported())

#endif  // LIB_ESCHER_TEST_GTEST_VULKAN_H_
