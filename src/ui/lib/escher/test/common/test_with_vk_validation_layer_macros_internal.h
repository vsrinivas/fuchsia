// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_TEST_COMMON_TEST_WITH_VK_VALIDATION_LAYER_MACROS_INTERNAL_H_
#define SRC_UI_LIB_ESCHER_TEST_COMMON_TEST_WITH_VK_VALIDATION_LAYER_MACROS_INTERNAL_H_

#include <gtest/gtest.h>

// Check if current class has |vk_debug_report_collector()| function (e.g. TestWithVkValidationLayer
// or its derived class) at compile-time.
#define CHECK_IS_TEST_WITH_VK_VALIDATION_LAYER_DEFAULT()                                   \
  static_assert(                                                                           \
      std::is_same<::escher::test::impl::VkDebugReportCollector,                           \
                   std::remove_reference_t<decltype(vk_debug_report_collector())>>::value, \
      "This macro can be only used on classes with |VkDebugReportCollector| element "      \
      "named |debug_report_collector_|.")

// Helper macros used to generate EXPECT_... macros for validation layer check.
#define EXPECT_VULKAN_VALIDATION_REPORT_PRED_(flags, display_flags, pred, pred_op, num_threshold)  \
  do {                                                                                             \
    GTEST_AMBIGUOUS_ELSE_BLOCKER_;                                                                 \
    CHECK_IS_TEST_WITH_VK_VALIDATION_LAYER_DEFAULT();                                              \
    if (!vk_debug_report_collector().PrintDebugReportsOnFalsePredicate(flags, num_threshold, pred, \
                                                                       __FILE__, __LINE__))        \
      GTEST_MESSAGE_AT_(__FILE__, __LINE__,                                                        \
                        "Number of debug reports with flag [" display_flags "] " #pred_op          \
                        " " #num_threshold ", test failed.",                                       \
                        ::testing::TestPartResult::kNonFatalFailure);                              \
  } while (0)

// Helper macros used to generate ASSERT_... macros for validation layer check.
#define ASSERT_VULKAN_VALIDATION_REPORT_PRED_(flags, display_flags, pred, pred_op, num_threshold)  \
  do {                                                                                             \
    GTEST_AMBIGUOUS_ELSE_BLOCKER_;                                                                 \
    CHECK_IS_TEST_WITH_VK_VALIDATION_LAYER_DEFAULT();                                              \
    if (!vk_debug_report_collector().PrintDebugReportsOnFalsePredicate(flags, num_threshold, pred, \
                                                                       __FILE__, __LINE__))        \
      return GTEST_MESSAGE_AT_(__FILE__, __LINE__,                                                 \
                               "Number of debug reports with flag [" display_flags "] " #pred_op   \
                               " " #num_threshold ", test failed.",                                \
                               ::testing::TestPartResult::kFatalFailure);                          \
  } while (0)

#define CHECK_VULKAN_VALIDATION_ERRORS_PRED_(fatal, pred, pred_op, num_threshold)           \
  fatal##_VULKAN_VALIDATION_REPORT_PRED_(vk::DebugReportFlagBitsEXT::eError, "ERROR", pred, \
                                         pred_op, num_threshold)
#define CHECK_VULKAN_VALIDATION_WARNINGS_PRED_(fatal, pred, pred_op, num_threshold)             \
  fatal##_VULKAN_VALIDATION_REPORT_PRED_(vk::DebugReportFlagBitsEXT::eWarning, "WARNING", pred, \
                                         pred_op, num_threshold)
#define CHECK_VULKAN_VALIDATION_PERFORMANCE_WARNINGS_PRED_(fatal, pred, pred_op, num_threshold) \
  fatal##_VULKAN_VALIDATION_REPORT_PRED_(vk::DebugReportFlagBitsEXT::ePerformanceWarning,       \
                                         "PERFORMANCE WARNING", pred, pred_op, num_threshold)

#define EXPECT_VULKAN_VALIDATION_REPORT_GENERATOR_(flag, pred, pred_op, num_threshold) \
  CHECK_VULKAN_VALIDATION_##flag##_PRED_(EXPECT, (pred){}, pred_op, num_threshold)
#define ASSERT_VULKAN_VALIDATION_REPORT_GENERATOR_(flag, pred, pred_op, num_threshold) \
  CHECK_VULKAN_VALIDATION_##flag##_PRED_(ASSERT, (pred){}, pred_op, num_threshold)

#endif  // SRC_UI_LIB_ESCHER_TEST_COMMON_TEST_WITH_VK_VALIDATION_LAYER_MACROS_INTERNAL_H_
