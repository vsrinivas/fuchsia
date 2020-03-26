// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_TEST_COMMON_TEST_WITH_VK_VALIDATION_LAYER_H_
#define SRC_UI_LIB_ESCHER_TEST_COMMON_TEST_WITH_VK_VALIDATION_LAYER_H_

#include "src/ui/lib/escher/test/common/test_with_vk_validation_layer_macros.h"
#include "src/ui/lib/escher/test/common/vk/vk_debug_report_callback_registry.h"
#include "src/ui/lib/escher/test/common/vk/vk_debug_report_collector.h"

namespace escher::test {

// Default Googletest fixture for checking Vulkan validation errors, warnings and performance
// warnings.
//
// Usage:
//
// 1) Tests need to use this as its test fixture:
//      using TestSuiteName = escher::test::TestWithVkValidationLayer;
//      VK_TEST_F(TestSuiteName, TestCaseName) {
//        ... // Test body
//      }
//    or derive directly from this class:
//      class DerivedTest: public escher::test::TestWithVkValidationLayer {
//        ... // Class definition
//      };
//      VK_TEST_F(DerivedTest, TestCaseName) {
//        ... // Test body
//      }
//
//    Note that if |SetUp()| is overridden, |TestWithVkValidationlayer::SetUp()| should be called in
//    its beginning; if |TearDown()| is overridden, |TestWithVkValidationlayer::TearDown()| should
//    be called in the end. See the documentation of |SetUp()| and |TearDown()| below for details.
//
// 2) For all |TestWithVkValidationLayer| tests, after the test ends, it will check if there are
//    Vulkan validation debug reports. The test will fail if there is any Vulkan validation error /
//    warning / performance warning.
//
//    To suppress the after-test validation check, run the following macro in the end of the test
//    body:
//      SUPPRESS_VK_VALIDATION_DEBUG_REPORTS()
//
//    or macro with specified message flags:
//      SUPPRESS_VK_VALIDATION_ERRORS()
//      SUPPRESS_VK_VALIDATION_WARNINGS()
//      SUPPRESS_VK_VALIDATION_PERFORMANCE_WARNINGS()
//
//    All the above macros can be only used when test fixture has |vk_debug_report_collector()|,
//    like this class and classes derived from this class.
//
// 3) Besides, one can also use the following macros to check Vulkan validation messages:
//
//      ASSERT_NO_VULKAN_VALIDATION_[ERROR-TYPE]()
//      EXPECT_NO_VULKAN_VALIDATION_[ERROR-TYPE]()
//      ASSERT_VULKAN_VALIDATION_[ERROR-TYPE]_[PRED](MAXIMUM_ERRORS)
//      EXPECT_VULKAN_VALIDATION_[ERROR-TYPE]_[PRED](MAXIMUM_ERRORS)
//    where
//      [ERROR-TYPE] := {ERRORS, WARNINGS, PERFORMANCE_WARNINGS},
//      [PRED] := {LE, LT, GE, GT, EQ, NE}.
//
//    There are also macros ASSERT_VULKAN_VALIDATION_OK() and EXPECT_VULKAN_VALIDATION_OK()
//    checking if there is any message belonging to errors, warnings or performance warnings.
//
//    Example:
//
//      using TestSuite = escher::test::TestWithVkValidationLayer;
//      VK_TEST_F(TestSuite, TestName) {
//        auto escher = escher::test::GetEscher();
//        ... // some Vulkan operations
//        ASSERT_NO_VULKAN_VALIDATION_ERRORS() << "Optional error message";
//
//        ... // some Vulkan operations
//        ASSERT_NO_VULKAN_VALIDATION_WARNINGS(1);
//      }
//
//    All the above macros can be only used when test fixture has |vk_debug_report_collector()|,
//    like this class and classes derived from this class.
//
// 4) Since this class has a |VkDebugReportCallbackRegistry| instance, it can also support optional
//    debug report callback functions by deriving this class and setting up extra callback
//    functions in its constructor.
//
class TestWithVkValidationLayer : public ::testing::Test {
 protected:
  TestWithVkValidationLayer()
      : TestWithVkValidationLayer(std::vector<VulkanInstance::DebugReportCallback>{}) {}
  TestWithVkValidationLayer(std::vector<VulkanInstance::DebugReportCallback> optional_callbacks);

  // |SetUp()| method of this class inherits from its parent class |::testing::Test|.
  //
  // Note: For all derived class, if they need to override this function, call this function first
  // in the new |SetUp()| function:
  //
  // void SetUp() override {
  //   TestWithVkValidationLayer::SetUp();
  //   ... // do something
  // }
  void SetUp() override;

  // Overrides |::testing::Test::TearDown()|.
  // |TearDown()| checks existence of validation messages and deregisters all debug report callback
  // functions.
  //
  // Note: For all derived class, if they need to override this function, call this function in the
  // end of the new |TearDown()| function:
  //
  // void TearDown() override {
  //  ... // do something
  //   TestWithVkValidationLayer::TearDown();
  // }
  void TearDown() override;

  impl::VkDebugReportCallbackRegistry& vk_debug_report_callback_registry() {
    return vk_debug_report_callback_registry_;
  }
  impl::VkDebugReportCollector& vk_debug_report_collector() { return vk_debug_report_collector_; }

 private:
  impl::VkDebugReportCallbackRegistry vk_debug_report_callback_registry_;
  impl::VkDebugReportCollector vk_debug_report_collector_;
};

}  // namespace escher::test

#endif  // SRC_UI_LIB_ESCHER_TEST_COMMON_TEST_WITH_VK_VALIDATION_LAYER_H_
