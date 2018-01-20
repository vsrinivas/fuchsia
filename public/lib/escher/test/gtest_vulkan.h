// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_PUBLIC_LIB_ESCHER_TEST_GTEST_VULKAN_H_
#define GARNET_PUBLIC_LIB_ESCHER_TEST_GTEST_VULKAN_H_

#include "garnet/public/lib/escher/test/gtest_vulkan_internal.h"

#define VK_TEST(test_case_name, test_name)                   \
  VK_GTEST_TEST_(test_case_name, test_name, ::testing::Test, \
                 ::testing::internal::GetTestTypeId())

#define VK_TEST_F(test_fixture, test_name)              \
  VK_GTEST_TEST_(test_fixture, test_name, test_fixture, \
                 ::testing::internal::GetTypeId<test_fixture>())

#define VK_TESTS_SUPPRESSED() \
  (!::testing::internal::escher::VulkanIsSupported())

#endif  // GARNET_PUBLIC_LIB_ESCHER_TEST_GTEST_VULKAN_H_
