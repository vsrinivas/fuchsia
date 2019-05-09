// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_TEST_GTEST_VULKAN_INTERNAL_H_
#define SRC_UI_LIB_ESCHER_TEST_GTEST_VULKAN_INTERNAL_H_

#include <string>

#include "gtest/gtest.h"

namespace testing {
namespace internal {
namespace escher {

// Callback used by MakeAndRegisterVulkanTestInfo() to create a test factory.
typedef TestFactoryBase* (*TestFactoryFactory)();

// If Vulkan is disabled, then prepend "DISABLED_" to test_case and return it,
// otherwise return it unmodified. This is used by the VK_GTEST_TEST_ macro to
// disable tests when required.
std::string PrependDisabledIfNecessary(const std::string& test_case);

// Template function that matches the TestFactoryFactory typedef above.
template <typename T>
TestFactoryBase* ConcreteTestFactoryFactory() {
  return new ::testing::internal::TestFactoryImpl<T>;
}

}  // namespace escher
}  // namespace internal
}  // namespace testing

// Helper macro for defining tests.
#define VK_GTEST_TEST_(test_case_name, test_name, parent_class, parent_id)    \
  class GTEST_TEST_CLASS_NAME_(test_case_name, test_name)                     \
      : public parent_class {                                                 \
   public:                                                                    \
    GTEST_TEST_CLASS_NAME_(test_case_name, test_name)() {}                    \
                                                                              \
   private:                                                                   \
    virtual void TestBody();                                                  \
    static ::testing::TestInfo* const test_info_ GTEST_ATTRIBUTE_UNUSED_;     \
    GTEST_DISALLOW_COPY_AND_ASSIGN_(GTEST_TEST_CLASS_NAME_(test_case_name,    \
                                                           test_name));       \
  };                                                                          \
                                                                              \
  ::testing::TestInfo* const GTEST_TEST_CLASS_NAME_(test_case_name,           \
                                                    test_name)::test_info_ =  \
      ::testing::RegisterTest(                                                \
          #test_case_name,                                                    \
          ::testing::internal::escher::PrependDisabledIfNecessary(#test_name) \
              .c_str(),                                                       \
          nullptr, nullptr, __FILE__, __LINE__, []() -> parent_class* {       \
            return new GTEST_TEST_CLASS_NAME_(test_case_name, test_name);     \
          });                                                                 \
  void GTEST_TEST_CLASS_NAME_(test_case_name, test_name)::TestBody()

#endif  // SRC_UI_LIB_ESCHER_TEST_GTEST_VULKAN_INTERNAL_H_
