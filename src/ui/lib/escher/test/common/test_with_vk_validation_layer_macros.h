// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_TEST_COMMON_TEST_WITH_VK_VALIDATION_LAYER_MACROS_H_
#define SRC_UI_LIB_ESCHER_TEST_COMMON_TEST_WITH_VK_VALIDATION_LAYER_MACROS_H_

#include "src/ui/lib/escher/test/common/test_with_vk_validation_layer_macros_internal.h"

// Note: All the macros below can be only used in classes containing  and its derived
// classes (e.g. TestWithVkValidationLayer)
//
// Disable code formatter to allow for longer line length.
// clang-format off

// By default, after test case in TestWithVkValidationLayer finishes, it will check if there
// are Vulkan validation debug reports, and there will be an EXPECT() failure if there is any
// validation errors / warnings / performance warnings.
//
// These macros suppress the after-test validation check by removing all debug reports (or all
// debug reports with specific flag bits).
#define SUPPRESS_VK_VALIDATION_DEBUG_REPORTS()             \
  do {                                                     \
    CHECK_IS_TEST_WITH_VK_VALIDATION_LAYER_DEFAULT();      \
    vk_debug_report_collector().SuppressAllDebugReports(); \
  } while (0)
#define SUPPRESS_VK_VALIDATION_ERRORS()                                                           \
  do {                                                                                            \
    CHECK_IS_TEST_WITH_VK_VALIDATION_LAYER_DEFAULT();                                             \
    vk_debug_report_collector().SuppressDebugReportsWithFlag(vk::DebugReportFlagBitsEXT::eError); \
  } while (0)
#define SUPPRESS_VK_VALIDATION_WARNINGS()                                                           \
  do {                                                                                              \
    CHECK_IS_TEST_WITH_VK_VALIDATION_LAYER_DEFAULT();                                               \
    vk_debug_report_collector().SuppressDebugReportsWithFlag(vk::DebugReportFlagBitsEXT::eWarning); \
  } while (0)
#define SUPPRESS_VK_VALIDATION_PERFORMANCE_WARNINGS()                                                          \
  do {                                                                                                         \
    CHECK_IS_TEST_WITH_VK_VALIDATION_LAYER_DEFAULT();                                                          \
    vk_debug_report_collector().SuppressDebugReportsWithFlag(vk::DebugReportFlagBitsEXT::ePerformanceWarning); \
  } while (0)

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

#define EXPECT_VULKAN_VALIDATION_OK()                   \
  do {                                                  \
    EXPECT_NO_VULKAN_VALIDATION_ERRORS();               \
    EXPECT_NO_VULKAN_VALIDATION_WARNINGS();             \
    EXPECT_NO_VULKAN_VALIDATION_PERFORMANCE_WARNINGS(); \
  } while (0)

#define ASSERT_VULKAN_VALIDATION_OK()                   \
  do {                                                  \
    ASSERT_NO_VULKAN_VALIDATION_ERRORS();               \
    ASSERT_NO_VULKAN_VALIDATION_WARNINGS();             \
    ASSERT_NO_VULKAN_VALIDATION_PERFORMANCE_WARNINGS(); \
  } while (0)
// clang-format on

#endif  // SRC_UI_LIB_ESCHER_TEST_COMMON_TEST_WITH_VK_VALIDATION_LAYER_MACROS_H_
