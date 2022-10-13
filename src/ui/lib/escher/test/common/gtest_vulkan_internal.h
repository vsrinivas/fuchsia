// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_TEST_COMMON_GTEST_VULKAN_INTERNAL_H_
#define SRC_UI_LIB_ESCHER_TEST_COMMON_GTEST_VULKAN_INTERNAL_H_

#include <string>

#include <gtest/gtest.h>

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
#define VK_GTEST_TEST_(test_case_name, test_name, parent_class, parent_id)                       \
  class GTEST_TEST_CLASS_NAME_(test_case_name, test_name) : public parent_class {                \
   public:                                                                                       \
    GTEST_TEST_CLASS_NAME_(test_case_name, test_name)() {}                                       \
    GTEST_TEST_CLASS_NAME_(test_case_name, test_name)                                            \
    (const GTEST_TEST_CLASS_NAME_(test_case_name, test_name) &) = delete;                        \
    GTEST_TEST_CLASS_NAME_(test_case_name, test_name) & operator=(                               \
        const GTEST_TEST_CLASS_NAME_(test_case_name, test_name) &) = delete;                     \
                                                                                                 \
   private:                                                                                      \
    virtual void TestBody();                                                                     \
    static ::testing::TestInfo* const test_info_ GTEST_ATTRIBUTE_UNUSED_;                        \
  };                                                                                             \
                                                                                                 \
  ::testing::TestInfo* const GTEST_TEST_CLASS_NAME_(test_case_name, test_name)::test_info_ =     \
      ::testing::internal::MakeAndRegisterTestInfo(                                              \
          #test_case_name,                                                                       \
          ::testing::internal::escher::PrependDisabledIfNecessary(#test_name).c_str(), nullptr,  \
          nullptr, ::testing::internal::CodeLocation(__FILE__, __LINE__), (parent_id),           \
          ::testing::internal::SuiteApiResolver<parent_class>::GetSetUpCaseOrSuite(__FILE__,     \
                                                                                   __LINE__),    \
          ::testing::internal::SuiteApiResolver<parent_class>::GetTearDownCaseOrSuite(__FILE__,  \
                                                                                      __LINE__), \
          new ::testing::internal::TestFactoryImpl<GTEST_TEST_CLASS_NAME_(test_case_name,        \
                                                                          test_name)>);          \
  void GTEST_TEST_CLASS_NAME_(test_case_name, test_name)::TestBody()

#define VK_GTEST_TEST_P_(test_suite_name, test_name)                                           \
  class GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) : public test_suite_name {          \
   public:                                                                                     \
    GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)() {}                                    \
    virtual void TestBody();                                                                   \
    GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)                                         \
    (const GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) &) = delete;                     \
    GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) & operator=(                            \
        const GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) &) = delete;                  \
                                                                                               \
   private:                                                                                    \
    static int AddToRegistry() {                                                               \
      ::testing::UnitTest::GetInstance()                                                       \
          ->parameterized_test_registry()                                                      \
          .GetTestSuitePatternHolder<test_suite_name>(                                         \
              #test_suite_name, ::testing::internal::CodeLocation(__FILE__, __LINE__))         \
          ->AddTestPattern(                                                                    \
              GTEST_STRINGIFY_(test_suite_name),                                               \
              ::testing::internal::escher::PrependDisabledIfNecessary(#test_name).c_str(),     \
              new ::testing::internal::TestMetaFactory<GTEST_TEST_CLASS_NAME_(test_suite_name, \
                                                                              test_name)>(),   \
              ::testing::internal::CodeLocation(__FILE__, __LINE__));                          \
      return 0;                                                                                \
    }                                                                                          \
    static int gtest_registering_dummy_ GTEST_ATTRIBUTE_UNUSED_;                               \
  };                                                                                           \
  int GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)::gtest_registering_dummy_ =           \
      GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)::AddToRegistry();                     \
  void GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)::TestBody()

#endif  // SRC_UI_LIB_ESCHER_TEST_COMMON_GTEST_VULKAN_INTERNAL_H_
