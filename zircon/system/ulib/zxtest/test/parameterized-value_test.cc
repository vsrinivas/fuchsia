// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>

#include <fbl/function.h>
#include <zxtest/base/assertion.h>
#include <zxtest/base/observer.h>
#include <zxtest/base/parameterized-value-impl.h>
#include <zxtest/base/runner.h>
#include <zxtest/base/test-case.h>
#include <zxtest/base/test-info.h>
#include <zxtest/base/types.h>
#include <zxtest/base/values.h>

namespace zxtest {

class RunnerTestPeer {
 public:
  static size_t GetParameterizedTestInfoSize(Runner* runner) {
    return runner->parameterized_test_info_.size();
  }

  static void DeleteParameterizedTestInfo(Runner* runner, const internal::TypeId& suite_type) {
    size_t index = 0;
    for (auto& test_info : runner->parameterized_test_info_) {
      if (test_info->GetFixtureId() == suite_type) {
        runner->parameterized_test_info_.erase(index);
        return;
      }
      index++;
    }
  }

  static std::optional<::zxtest::internal::ParameterizedTestCaseInfo*> GetParameterizedTestInfo(
      Runner* runner, const internal::TypeId& suite_type) {
    for (auto& test_info : runner->parameterized_test_info_) {
      if (test_info->GetFixtureId() == suite_type) {
        return test_info.get();
      }
    }
    return {};
  }
};

namespace internal {
class ParameterizedTestCaseInfoImplTestPeer {
 public:
  template <typename T, typename U>
  static size_t GetEntriesSize(ParameterizedTestCaseInfoImpl<T, U>* suite) {
    return suite->test_entries_.size();
  }

  template <typename T, typename U>
  static size_t GetInstantiationsSize(ParameterizedTestCaseInfoImpl<T, U>* suite) {
    return suite->instantiation_fns_.size();
  }
};
}  // namespace internal

namespace test {

class ParameterizedTestSuite1 : public TestWithParam<int> {
 public:
  void SetUp() override {}

  void TearDown() override {}
};

class ParameterizedSuite1Test1 : public ParameterizedTestSuite1 {
 private:
  void TestBody() final {}
};

class ParameterizedSuite1Test2 : public ParameterizedTestSuite1 {
 private:
  void TestBody() final {}
};

class ParameterizedTestSuite2 : public TestWithParam<int> {
 public:
  void SetUp() override {}

  void TearDown() override {}
};

class ParameterizedSuite2Test1 : public ParameterizedTestSuite2 {
 private:
  void TestBody() final {}
};

void TestAddParameterizedSuites() {
  auto runner = ::zxtest::Runner::GetInstance();
  auto type1 = internal::TypeIdProvider<ParameterizedTestSuite1>::Get();
  auto type2 = internal::TypeIdProvider<ParameterizedTestSuite2>::Get();
  auto orig_size = ::zxtest::RunnerTestPeer::GetParameterizedTestInfoSize(runner);
  ZX_ASSERT_MSG(!::zxtest::RunnerTestPeer::GetParameterizedTestInfo(runner, type1).has_value(),
                "The test suite should not exist yet.");

  runner->AddParameterizedTest<ParameterizedTestSuite1>(
      std::make_unique<::zxtest::internal::AddTestDelegateImpl<
          ParameterizedTestSuite1, ParameterizedTestSuite1::ParamType, ParameterizedSuite1Test1>>(),
      fbl::String("suite_name"), fbl::String("test_name"),
      {.filename = __FILE__, .line_number = __LINE__});

  ZX_ASSERT_MSG(::zxtest::RunnerTestPeer::GetParameterizedTestInfo(runner, type1).has_value(),
                "There should be a matching test suite.");
  ZX_ASSERT_MSG(::zxtest::RunnerTestPeer::GetParameterizedTestInfoSize(runner) == orig_size + 1,
                "The number of suites should have increased.");

  runner->AddParameterizedTest<ParameterizedTestSuite2>(
      std::make_unique<::zxtest::internal::AddTestDelegateImpl<
          ParameterizedTestSuite2, ParameterizedTestSuite2::ParamType, ParameterizedSuite2Test1>>(),
      fbl::String("suite_name"), fbl::String("test_name"),
      {.filename = __FILE__, .line_number = __LINE__});

  ZX_ASSERT_MSG(::zxtest::RunnerTestPeer::GetParameterizedTestInfo(runner, type2).has_value(),
                "There should be a matching test suite.");
  ZX_ASSERT_MSG(::zxtest::RunnerTestPeer::GetParameterizedTestInfoSize(runner) == orig_size + 2,
                "The number of suites should have increased.");

  ::zxtest::RunnerTestPeer::DeleteParameterizedTestInfo(runner, type1);
  ::zxtest::RunnerTestPeer::DeleteParameterizedTestInfo(runner, type2);
}

void TestAddParameterizedTests() {
  auto runner = ::zxtest::Runner::GetInstance();
  auto type = internal::TypeIdProvider<ParameterizedTestSuite1>::Get();
  auto orig_size = ::zxtest::RunnerTestPeer::GetParameterizedTestInfoSize(runner);
  ZX_ASSERT_MSG(!::zxtest::RunnerTestPeer::GetParameterizedTestInfo(runner, type).has_value(),
                "The test suite should not exist yet.");

  runner->AddParameterizedTest<ParameterizedTestSuite1>(
      std::make_unique<::zxtest::internal::AddTestDelegateImpl<
          ParameterizedTestSuite1, ParameterizedTestSuite1::ParamType, ParameterizedSuite1Test1>>(),
      fbl::String("suite_name"), fbl::String("test_name"),
      {.filename = __FILE__, .line_number = __LINE__});

  std::optional<internal::ParameterizedTestCaseInfo*> suite =
      ::zxtest::RunnerTestPeer::GetParameterizedTestInfo(runner, type);
  ZX_ASSERT_MSG(suite.has_value(), "There should be a matching test suite.");
  ZX_ASSERT_MSG(::zxtest::RunnerTestPeer::GetParameterizedTestInfoSize(runner) == orig_size + 1,
                "The number of suites should have increased.");
  auto val = suite.value();
  using ImplType =
      typename internal::ParameterizedTestCaseInfoImpl<ParameterizedTestSuite1,
                                                       ParameterizedTestSuite1::ParamType>;
  ImplType* suite_impl = reinterpret_cast<ImplType*>(val);

  ZX_ASSERT_MSG(internal::ParameterizedTestCaseInfoImplTestPeer::GetEntriesSize(suite_impl) == 1,
                "There should only be one test case entry.");

  runner->AddParameterizedTest<ParameterizedTestSuite1>(
      std::make_unique<::zxtest::internal::AddTestDelegateImpl<
          ParameterizedTestSuite1, ParameterizedTestSuite1::ParamType, ParameterizedSuite1Test2>>(),
      fbl::String("suite_name"), fbl::String("test_name"),
      {.filename = __FILE__, .line_number = __LINE__});
  ZX_ASSERT_MSG(::zxtest::RunnerTestPeer::GetParameterizedTestInfoSize(runner) == orig_size + 1,
                "The number of suites should not have changed.");
  ZX_ASSERT_MSG(internal::ParameterizedTestCaseInfoImplTestPeer::GetEntriesSize(suite_impl) == 2,
                "There should be two test case entries.");

  ::zxtest::RunnerTestPeer::DeleteParameterizedTestInfo(runner, type);
}

void TestAddParameterizedInstaniations() {
  auto runner = ::zxtest::Runner::GetInstance();
  auto type = internal::TypeIdProvider<ParameterizedTestSuite1>::Get();
  ZX_ASSERT_MSG(!::zxtest::RunnerTestPeer::GetParameterizedTestInfo(runner, type).has_value(),
                "The test suite should not exist yet.");

  runner->AddParameterizedTest<ParameterizedTestSuite1>(
      std::make_unique<::zxtest::internal::AddTestDelegateImpl<
          ParameterizedTestSuite1, ParameterizedTestSuite1::ParamType, ParameterizedSuite1Test1>>(),
      fbl::String("suite_name"), fbl::String("test_name"),
      {.filename = __FILE__, .line_number = __LINE__});

  std::optional<internal::ParameterizedTestCaseInfo*> suite =
      ::zxtest::RunnerTestPeer::GetParameterizedTestInfo(runner, type);
  ZX_ASSERT_MSG(suite.has_value(), "There should be a matching test suite.");
  auto val = suite.value();
  using ImplType =
      typename internal::ParameterizedTestCaseInfoImpl<ParameterizedTestSuite1,
                                                       ParameterizedTestSuite1::ParamType>;
  ImplType* suite_impl = reinterpret_cast<ImplType*>(val);
  ZX_ASSERT_MSG(
      internal::ParameterizedTestCaseInfoImplTestPeer::GetInstantiationsSize(suite_impl) == 0,
      "There should be no instantiation entries yet.");

  auto provider1 = ::zxtest::testing::Values(1, 2, 3);
  runner->AddInstantiation<ParameterizedTestSuite1, ParameterizedTestSuite1::ParamType>(
      std::make_unique<zxtest::internal::AddInstantiationDelegateImpl<
          ParameterizedTestSuite1, ParameterizedTestSuite1::ParamType>>(),
      fbl::String("prefix_name"), {.filename = __FILE__, .line_number = __LINE__}, provider1);
  ZX_ASSERT_MSG(
      internal::ParameterizedTestCaseInfoImplTestPeer::GetInstantiationsSize(suite_impl) == 1,
      "There should be one instantiation entry.");

  auto provider2 = ::zxtest::testing::Values(5, 4, 3);
  runner->AddInstantiation<ParameterizedTestSuite1, ParameterizedTestSuite1::ParamType>(
      std::make_unique<zxtest::internal::AddInstantiationDelegateImpl<
          ParameterizedTestSuite1, ParameterizedTestSuite1::ParamType>>(),
      fbl::String("prefix_name"), {.filename = __FILE__, .line_number = __LINE__}, provider2);
  ZX_ASSERT_MSG(
      internal::ParameterizedTestCaseInfoImplTestPeer::GetInstantiationsSize(suite_impl) == 2,
      "There should be two instantiation entries.");

  ::zxtest::RunnerTestPeer::DeleteParameterizedTestInfo(runner, type);
}
}  // namespace test
}  // namespace zxtest
