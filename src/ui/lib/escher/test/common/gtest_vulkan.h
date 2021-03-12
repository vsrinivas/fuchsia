// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_TEST_COMMON_GTEST_VULKAN_H_
#define SRC_UI_LIB_ESCHER_TEST_COMMON_GTEST_VULKAN_H_

#include "src/ui/lib/escher/test/common/gtest_vulkan_internal.h"
#include "src/ui/lib/escher/util/check_vulkan_support.h"

#include <vulkan/vulkan.hpp>

// Tests declared using this macro are only registered with GTest when Vulkan is available.
#define VK_TEST(test_case_name, test_name) \
  VK_GTEST_TEST_(test_case_name, test_name, ::testing::Test, ::testing::internal::GetTestTypeId())

// Tests declared using this macro are only registered with GTest when Vulkan is available.
#define VK_TEST_F(test_fixture, test_name)              \
  VK_GTEST_TEST_(test_fixture, test_name, test_fixture, \
                 ::testing::internal::GetTypeId<test_fixture>())

// Tests declared using this macro are only registered with GTest when Vulkan is available.
// This is used for creating value-parameterized test suites. It also requires
// INSTANTIATE_TEST_SUITE_P to instantiate test suites, see gtest.h for details.
#define VK_TEST_P(test_fixture, test_name) VK_GTEST_TEST_P_(test_fixture, test_name)

// Tests that require Vulkan are suppressed if Vulkan is not supported.
#define VK_TESTS_SUPPRESSED() (!escher::VulkanIsSupported())

#endif  // SRC_UI_LIB_ESCHER_TEST_COMMON_GTEST_VULKAN_H_
