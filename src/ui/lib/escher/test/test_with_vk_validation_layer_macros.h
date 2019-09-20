// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_TEST_TEST_WITH_VK_VALIDATION_LAYER_MACROS_H_
#define SRC_UI_LIB_ESCHER_TEST_TEST_WITH_VK_VALIDATION_LAYER_MACROS_H_

#include "src/ui/lib/escher/test/test_with_vk_validation_layer_macros_internal.h"

// Note: All the macros below can be only used in TestWithVkValidationLayer and its derived
// classes.
//
// Disable code formatter to allow for longer line length.
// clang-format off

// By default, after test case in TestWithVkValidationLayer finishes, it will check if there
// are Vulkan validation debug reports, and there will be an EXPECT() failure if there is any
// validation errors / warnings / performance warnings.
//
// These macros suppress the after-test validation check by removing all debug reports (or all
// debug reports with specific flag bits).
#define SUPPRESS_VK_VALIDATION_DEBUG_REPORTS()      \
  CHECK_IS_TEST_WITH_VK_VALIDATION_LAYER_DEFAULT(); \
  SuppressAllDebugReports_()
#define SUPPRESS_VK_VALIDATION_ERRORS()             \
  CHECK_IS_TEST_WITH_VK_VALIDATION_LAYER_DEFAULT(); \
  SuppressDebugReportsWithFlag_(vk::DebugReportFlagBitsEXT::eError)
#define SUPPRESS_VK_VALIDATION_WARNINGS()           \
  CHECK_IS_TEST_WITH_VK_VALIDATION_LAYER_DEFAULT(); \
  SuppressDebugReportsWithFlag_(vk::DebugReportFlagBitsEXT::eWarning)
#define SUPPRESS_VK_VALIDATION_PERFORMANCE_WARNINGS() \
  CHECK_IS_TEST_WITH_VK_VALIDATION_LAYER_DEFAULT();   \
  SuppressDebugReportsWithFlag_(vk::DebugReportFlagBitsEXT::ePerformanceWarning)

// Vulkan validation message check macros.
// EXPECT_... macro will not terminate the test when it fails.
#define EXPECT_VULKAN_VALIDATION_ERRORS_GE(num_threshold) EXPECT_VULKAN_VALIDATION_REPORT_GENERATOR_(ERRORS, std::greater_equal<size_t>, <, num_threshold)
#define EXPECT_VULKAN_VALIDATION_ERRORS_GT(num_threshold) EXPECT_VULKAN_VALIDATION_REPORT_GENERATOR_(ERRORS, std::greater<size_t>,       <=, num_threshold)
#define EXPECT_VULKAN_VALIDATION_ERRORS_LE(num_threshold) EXPECT_VULKAN_VALIDATION_REPORT_GENERATOR_(ERRORS, std::less_equal<size_t>,    >, num_threshold)
#define EXPECT_VULKAN_VALIDATION_ERRORS_LT(num_threshold) EXPECT_VULKAN_VALIDATION_REPORT_GENERATOR_(ERRORS, std::less<size_t>,          >=, num_threshold)
#define EXPECT_VULKAN_VALIDATION_ERRORS_EQ(num_threshold) EXPECT_VULKAN_VALIDATION_REPORT_GENERATOR_(ERRORS, std::equal_to<size_t>,      !=, num_threshold)
#define EXPECT_VULKAN_VALIDATION_ERRORS_NE(num_threshold) EXPECT_VULKAN_VALIDATION_REPORT_GENERATOR_(ERRORS, std::not_equal_to<size_t>,  ==, num_threshold)

#define EXPECT_VULKAN_VALIDATION_WARNINGS_GE(num_threshold) EXPECT_VULKAN_VALIDATION_REPORT_GENERATOR_(WARNINGS, std::greater_equal<size_t>, <, num_threshold)
#define EXPECT_VULKAN_VALIDATION_WARNINGS_GT(num_threshold) EXPECT_VULKAN_VALIDATION_REPORT_GENERATOR_(WARNINGS, std::greater<size_t>,       <=, num_threshold)
#define EXPECT_VULKAN_VALIDATION_WARNINGS_LE(num_threshold) EXPECT_VULKAN_VALIDATION_REPORT_GENERATOR_(WARNINGS, std::less_equal<size_t>,    >, num_threshold)
#define EXPECT_VULKAN_VALIDATION_WARNINGS_LT(num_threshold) EXPECT_VULKAN_VALIDATION_REPORT_GENERATOR_(WARNINGS, std::less<size_t>,          >=, num_threshold)
#define EXPECT_VULKAN_VALIDATION_WARNINGS_EQ(num_threshold) EXPECT_VULKAN_VALIDATION_REPORT_GENERATOR_(WARNINGS, std::equal_to<size_t>,      !=, num_threshold)
#define EXPECT_VULKAN_VALIDATION_WARNINGS_NE(num_threshold) EXPECT_VULKAN_VALIDATION_REPORT_GENERATOR_(WARNINGS, std::not_equal_to<size_t>,  ==, num_threshold)

#define EXPECT_VULKAN_VALIDATION_PERFORMANCE_WARNINGS_GE(num_threshold) EXPECT_VULKAN_VALIDATION_REPORT_GENERATOR_(PERFORMANCE_WARNINGS, std::greater_equal<size_t>, <,  num_threshold)
#define EXPECT_VULKAN_VALIDATION_PERFORMANCE_WARNINGS_GT(num_threshold) EXPECT_VULKAN_VALIDATION_REPORT_GENERATOR_(PERFORMANCE_WARNINGS, std::greater<size_t>,       <=, num_threshold)
#define EXPECT_VULKAN_VALIDATION_PERFORMANCE_WARNINGS_LE(num_threshold) EXPECT_VULKAN_VALIDATION_REPORT_GENERATOR_(PERFORMANCE_WARNINGS, std::less_equal<size_t>,    >,  num_threshold)
#define EXPECT_VULKAN_VALIDATION_PERFORMANCE_WARNINGS_LT(num_threshold) EXPECT_VULKAN_VALIDATION_REPORT_GENERATOR_(PERFORMANCE_WARNINGS, std::less<size_t>,          >=, num_threshold)
#define EXPECT_VULKAN_VALIDATION_PERFORMANCE_WARNINGS_EQ(num_threshold) EXPECT_VULKAN_VALIDATION_REPORT_GENERATOR_(PERFORMANCE_WARNINGS, std::equal_to<size_t>,      !=, num_threshold)
#define EXPECT_VULKAN_VALIDATION_PERFORMANCE_WARNINGS_NE(num_threshold) EXPECT_VULKAN_VALIDATION_REPORT_GENERATOR_(PERFORMANCE_WARNINGS, std::not_equal_to<size_t>,  ==, num_threshold)

// ASSERT_... macro will terminate the test when it fails.
#define ASSERT_VULKAN_VALIDATION_ERRORS_GE(num_threshold) ASSERT_VULKAN_VALIDATION_REPORT_GENERATOR_(ERRORS, std::greater_equal<size_t>, <, num_threshold)
#define ASSERT_VULKAN_VALIDATION_ERRORS_GT(num_threshold) ASSERT_VULKAN_VALIDATION_REPORT_GENERATOR_(ERRORS, std::greater<size_t>,       <=, num_threshold)
#define ASSERT_VULKAN_VALIDATION_ERRORS_LE(num_threshold) ASSERT_VULKAN_VALIDATION_REPORT_GENERATOR_(ERRORS, std::less_equal<size_t>,    >, num_threshold)
#define ASSERT_VULKAN_VALIDATION_ERRORS_LT(num_threshold) ASSERT_VULKAN_VALIDATION_REPORT_GENERATOR_(ERRORS, std::less<size_t>,          >=, num_threshold)
#define ASSERT_VULKAN_VALIDATION_ERRORS_EQ(num_threshold) ASSERT_VULKAN_VALIDATION_REPORT_GENERATOR_(ERRORS, std::equal_to<size_t>,      !=, num_threshold)
#define ASSERT_VULKAN_VALIDATION_ERRORS_NE(num_threshold) ASSERT_VULKAN_VALIDATION_REPORT_GENERATOR_(ERRORS, std::not_equal_to<size_t>,  ==, num_threshold)

#define ASSERT_VULKAN_VALIDATION_WARNINGS_GE(num_threshold) ASSERT_VULKAN_VALIDATION_REPORT_GENERATOR_(WARNINGS, std::greater_equal<size_t>, <, num_threshold)
#define ASSERT_VULKAN_VALIDATION_WARNINGS_GT(num_threshold) ASSERT_VULKAN_VALIDATION_REPORT_GENERATOR_(WARNINGS, std::greater<size_t>,       <=, num_threshold)
#define ASSERT_VULKAN_VALIDATION_WARNINGS_LE(num_threshold) ASSERT_VULKAN_VALIDATION_REPORT_GENERATOR_(WARNINGS, std::less_equal<size_t>,    >, num_threshold)
#define ASSERT_VULKAN_VALIDATION_WARNINGS_LT(num_threshold) ASSERT_VULKAN_VALIDATION_REPORT_GENERATOR_(WARNINGS, std::less<size_t>,          >=, num_threshold)
#define ASSERT_VULKAN_VALIDATION_WARNINGS_EQ(num_threshold) ASSERT_VULKAN_VALIDATION_REPORT_GENERATOR_(WARNINGS, std::equal_to<size_t>,      !=, num_threshold)
#define ASSERT_VULKAN_VALIDATION_WARNINGS_NE(num_threshold) ASSERT_VULKAN_VALIDATION_REPORT_GENERATOR_(WARNINGS, std::not_equal_to<size_t>,  ==, num_threshold)

#define ASSERT_VULKAN_VALIDATION_PERFORMANCE_WARNINGS_GE(num_threshold) ASSERT_VULKAN_VALIDATION_REPORT_GENERATOR_(PERFORMANCE_WARNINGS, std::greater_equal<size_t>, <,  num_threshold)
#define ASSERT_VULKAN_VALIDATION_PERFORMANCE_WARNINGS_GT(num_threshold) ASSERT_VULKAN_VALIDATION_REPORT_GENERATOR_(PERFORMANCE_WARNINGS, std::greater<size_t>,       <=, num_threshold)
#define ASSERT_VULKAN_VALIDATION_PERFORMANCE_WARNINGS_LE(num_threshold) ASSERT_VULKAN_VALIDATION_REPORT_GENERATOR_(PERFORMANCE_WARNINGS, std::less_equal<size_t>,    >,  num_threshold)
#define ASSERT_VULKAN_VALIDATION_PERFORMANCE_WARNINGS_LT(num_threshold) ASSERT_VULKAN_VALIDATION_REPORT_GENERATOR_(PERFORMANCE_WARNINGS, std::less<size_t>,          >=, num_threshold)
#define ASSERT_VULKAN_VALIDATION_PERFORMANCE_WARNINGS_EQ(num_threshold) ASSERT_VULKAN_VALIDATION_REPORT_GENERATOR_(PERFORMANCE_WARNINGS, std::equal_to<size_t>,      !=, num_threshold)
#define ASSERT_VULKAN_VALIDATION_PERFORMANCE_WARNINGS_NE(num_threshold) ASSERT_VULKAN_VALIDATION_REPORT_GENERATOR_(PERFORMANCE_WARNINGS, std::not_equal_to<size_t>,  ==, num_threshold)

#define EXPECT_NO_VULKAN_VALIDATION_ERRORS() EXPECT_VULKAN_VALIDATION_ERRORS_EQ(0)
#define EXPECT_NO_VULKAN_VALIDATION_WARNINGS() EXPECT_VULKAN_VALIDATION_WARNINGS_EQ(0)
#define EXPECT_NO_VULKAN_VALIDATION_PERFORMANCE_WARNINGS() \
  EXPECT_VULKAN_VALIDATION_PERFORMANCE_WARNINGS_EQ(0)

#define ASSERT_NO_VULKAN_VALIDATION_ERRORS() ASSERT_VULKAN_VALIDATION_ERRORS_EQ(0)
#define ASSERT_NO_VULKAN_VALIDATION_WARNINGS() ASSERT_VULKAN_VALIDATION_WARNINGS_EQ(0)
#define ASSERT_NO_VULKAN_VALIDATION_PERFORMANCE_WARNINGS() \
  ASSERT_VULKAN_VALIDATION_PERFORMANCE_WARNINGS_EQ(0)

#define EXPECT_VULKAN_VALIDATION_OK()     \
  EXPECT_NO_VULKAN_VALIDATION_ERRORS();   \
  EXPECT_NO_VULKAN_VALIDATION_WARNINGS(); \
  EXPECT_NO_VULKAN_VALIDATION_PERFORMANCE_WARNINGS()

#define ASSERT_VULKAN_VALIDATION_OK()     \
  ASSERT_NO_VULKAN_VALIDATION_ERRORS();   \
  ASSERT_NO_VULKAN_VALIDATION_WARNINGS(); \
  ASSERT_NO_VULKAN_VALIDATION_PERFORMANCE_WARNINGS()
// clang-format on

#endif  // SRC_UI_LIB_ESCHER_TEST_TEST_WITH_VK_VALIDATION_LAYER_MACROS_H_
