// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_BASE_PARAMETERIZED_VALUE_IMPL_H_
#define ZXTEST_BASE_PARAMETERIZED_VALUE_IMPL_H_

#include <lib/stdcompat/string_view.h>

#include <zxtest/base/parameterized-value.h>
#include <zxtest/base/runner.h>

namespace zxtest::internal {

// Templated version of ParameterizedTestCase. This class provides the implementation for the
// interface the registry relies on.
template <typename T, typename U>
class ParameterizedTestCaseInfoImpl : public ParameterizedTestCaseInfo {
 public:
  using FixtureType = T;
  using ParamType = U;

  // Returns a collection of factories for all registered tests of a test case.
  using ParameterizedTestFactory =
      fbl::Function<internal::TestFactory(fit::function<const ParamType&()>)>;

  ParameterizedTestCaseInfoImpl() = default;
  explicit ParameterizedTestCaseInfoImpl(const fbl::String& test_case_name)
      : ParameterizedTestCaseInfo(test_case_name) {}
  ParameterizedTestCaseInfoImpl(const ParameterizedTestCaseInfoImpl&) = delete;
  ParameterizedTestCaseInfoImpl(ParameterizedTestCaseInfoImpl&&) = delete;
  ParameterizedTestCaseInfoImpl& operator=(const ParameterizedTestCaseInfoImpl&) = delete;
  ParameterizedTestCaseInfoImpl& operator=(ParameterizedTestCaseInfoImpl&&) = delete;
  ~ParameterizedTestCaseInfoImpl() override = default;

  // Returns a unique Id representing the first fixture used to instantiate this Parametrized test
  // case.
  TypeId GetFixtureId() const final { return TypeIdProvider<FixtureType>::Get(); }

  void AddInstantiation(const fbl::String& instantiation_name,
                        zxtest::internal::ValueProvider<ParamType>& provider,
                        const SourceLocation& location) {
    instantiation_fns_.push_back([this, provider = std::move(provider), location,
                                  instantiation_name](Runner* runner) mutable {
      Instantiate<FixtureType, ParamType>(instantiation_name, location, provider, runner);
    });
  }

  // Adds a test to the test case.
  template <typename TestImpl>
  void AddTest(const fbl::String& name, const SourceLocation& location) {
    static_assert(std::is_base_of<FixtureType, TestImpl>::value,
                  "Must inherit from the same fixture to be part of the same test case.");
    TestInfo info;
    info.name = name;
    info.location = location;
    info.factory = &TestImpl::template CreateFactory<TestImpl>;
    test_entries_.push_back(std::move(info));
  }

  // Registers all parametrized tests of this test case with |runner|.
  void RegisterTest(Runner* runner) final {
    for (auto& instantiation_fn : instantiation_fns_) {
      instantiation_fn(runner);
    }
  }

 private:
  friend class ParameterizedTestCaseInfoImplTestPeer;

  struct TestInfo {
    fbl::String name;
    SourceLocation location;
    ParameterizedTestFactory factory;
  };

  template <typename TestImpl, typename ValueType>
  void Instantiate(const fbl::String& instantiation_name, const SourceLocation& location,
                   zxtest::internal::ValueProvider<ValueType>& provider, Runner* runner) {
    for (size_t i = 0; i < provider.size(); ++i) {
      for (auto& test_entry : test_entries_) {
        // Add method for instantiation name as a param, and let the reporter decide how to
        // print this.
        std::initializer_list<fbl::String> prefix_name = {instantiation_name, fbl::String("/"),
                                                          name()};
        std::initializer_list<fbl::String> test_name = {test_entry.name, fbl::String("_"),
                                                        std::to_string(i)};
        runner->RegisterTest<FixtureType, TestImpl>(
            fbl::String::Concat(prefix_name), fbl::String::Concat(test_name),
            test_entry.location.filename, static_cast<int>(test_entry.location.line_number),
            test_entry.factory(
                [&provider, i]() mutable -> const ValueType& { return provider[i]; }));
      }
    }
  }

  fbl::Vector<fbl::Function<void(Runner* runner)>> instantiation_fns_;
  fbl::Vector<TestInfo> test_entries_;
  fbl::String name_;
};

template <typename SuiteClass, typename Type, typename TestClass>
class AddTestDelegateImpl : public AddTestDelegate {
 public:
  std::unique_ptr<ParameterizedTestCaseInfo> CreateSuite(
      const cpp17::string_view& suite_name) final {
    return std::make_unique<ParameterizedTestCaseInfoImpl<SuiteClass, Type>>(suite_name);
  }

  bool AddTest(ParameterizedTestCaseInfo* base, const cpp17::string_view& test_name,
               const SourceLocation& location) final {
    ZX_ASSERT_MSG(base->GetFixtureId() == TypeIdProvider<SuiteClass>::Get(),
                  "ParameterizedTestCaseInfo type must match the suite type.");
    ParameterizedTestCaseInfoImpl<SuiteClass, Type>* suite_impl =
        reinterpret_cast<ParameterizedTestCaseInfoImpl<SuiteClass, Type>*>(base);
    suite_impl->template AddTest<TestClass>(test_name, location);
    return true;
  }
};

template <typename SuiteClass, typename Type>
class AddInstantiationDelegateImpl : public AddInstantiationDelegate<Type> {
 public:
  bool AddInstantiation(ParameterizedTestCaseInfo* base, const fbl::String& instantiation_name,
                        const SourceLocation& location,
                        zxtest::internal::ValueProvider<Type>& provider) final {
    ParameterizedTestCaseInfoImpl<SuiteClass, Type>* suite_impl =
        reinterpret_cast<ParameterizedTestCaseInfoImpl<SuiteClass, Type>*>(base);
    // ValueProvider<Type>& provider =
    // reinterpret_cast<ValueProvider<Type>&>(provider_base);
    suite_impl->AddInstantiation(instantiation_name, provider, location);
    return true;
  }
};

}  // namespace zxtest::internal

#endif  // ZXTEST_BASE_PARAMETERIZED_VALUE_IMPL_H_
