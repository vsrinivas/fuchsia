// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_TEST_GTEST_VULKAN_INTERNAL_H_
#define LIB_ESCHER_TEST_GTEST_VULKAN_INTERNAL_H_

#include "gtest/gtest.h"

namespace testing {
namespace internal {
namespace escher {

// Callback used by MakeAndRegisterVulkanTestInfo() to create a test factory.
typedef TestFactoryBase* (*TestFactoryFactory)();

// Wrapper around GTest's internal MakeAndRegisterTestInfo(), intended to
// support the VK_TEST() and VK_TEST_F() macros... see below.
GTEST_API_ TestInfo* MakeAndRegisterVulkanTestInfo(
    const char* test_case_name, const char* name, const char* type_param,
    const char* value_param, CodeLocation code_location,
    TypeId fixture_class_id, SetUpTestCaseFunc set_up_tc,
    TearDownTestCaseFunc tear_down_tc, TestFactoryFactory factory_factory);

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
      ::testing::internal::escher::MakeAndRegisterVulkanTestInfo(             \
          #test_case_name, #test_name, NULL, NULL,                            \
          ::testing::internal::CodeLocation(__FILE__, __LINE__), (parent_id), \
          parent_class::SetUpTestCase, parent_class::TearDownTestCase,        \
          ::testing::internal::escher::ConcreteTestFactoryFactory<            \
              GTEST_TEST_CLASS_NAME_(test_case_name, test_name)>);            \
  void GTEST_TEST_CLASS_NAME_(test_case_name, test_name)::TestBody()

#endif  // LIB_ESCHER_TEST_GTEST_VULKAN_INTERNAL_H_
