// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_BASE_PARAMETERIZED_VALUE_H_
#define ZXTEST_BASE_PARAMETERIZED_VALUE_H_

#include <lib/stdcompat/string_view.h>
#include <zircon/assert.h>

#include <optional>

#include <fbl/string.h>
#include <fbl/vector.h>
#include <zxtest/base/observer.h>
#include <zxtest/base/types.h>
#include <zxtest/base/values.h>

namespace zxtest {

template <class ParamType>
struct TestParamInfo {
  TestParamInfo(const ParamType& a_param, size_t an_index) : param(a_param), index(an_index) {}
  ParamType param;
  size_t index;
};

template <typename T>
class WithParamInterface {
 public:
  using ParamType = T;

  template <typename TestImpl>
  static internal::TestFactory CreateFactory(fit::function<const ParamType&()> value_getter) {
    return [value_getter = std::move(value_getter)](
               internal::TestDriver* driver) mutable -> std::unique_ptr<TestImpl> {
      TestImpl::param_.emplace(value_getter());
      std::unique_ptr<TestImpl> test = TestImpl::template Create<TestImpl>(driver);
      return std::move(test);
    };
  }

  virtual ~WithParamInterface() = default;

  const ParamType& GetParam() const { return WithParamInterface<T>::param_.value(); }

 protected:
  WithParamInterface() = default;

 private:
  static std::optional<ParamType> param_;
};

template <typename T>
std::optional<T> WithParamInterface<T>::param_ = std::nullopt;

// Interface for Value Parameterized tests. This class also captures the type
// of the parameter and provides storage for such parameter type. This follows gTest interface
// to allow more familiarity for users.
template <typename T>
class TestWithParam : public zxtest::Test, public WithParamInterface<T> {
 public:
  TestWithParam() = default;
  ~TestWithParam() override = default;
};

namespace internal {

// Alias for a trick to provide minimal RTTI to prevent invalid test instantiations.
using TypeId = const void*;

// This class is meant to provide a unique ID per type, which can be used at runtime to prevent
// parameterized test cases to collide. Since the type is hidden under an interface, we need to
// validate that we are not mixing fixtures, which will prevent wrong SetUp/TearDown from happening.
// This works because the compiler will allocate a boolean for each type, and we can rely on its
// address uniqueness for comparing whether two types are equal or not. Any other operation is not
// permitted.
template <typename T>
struct TypeIdProvider {
 public:
  // Returns a unique id for a given type.
  static TypeId Get() { return &unique_addr_; }

 private:
  // This is not marked const to prevent compiler optimization.
  static bool unique_addr_;
};

template <typename T>
bool TypeIdProvider<T>::unique_addr_ = false;

// This class provides an interface for an instantiation of a WithParamInterface to defer
// registration.
class ParameterizedTestCaseInfo {
 public:
  ParameterizedTestCaseInfo() = default;
  explicit ParameterizedTestCaseInfo(const fbl::String& test_case_name) : name_(test_case_name) {}
  ParameterizedTestCaseInfo(const ParameterizedTestCaseInfo&) = delete;
  ParameterizedTestCaseInfo(ParameterizedTestCaseInfo&&) = delete;
  ParameterizedTestCaseInfo& operator=(const ParameterizedTestCaseInfo&) = delete;
  ParameterizedTestCaseInfo& operator=(ParameterizedTestCaseInfo&&) = delete;
  virtual ~ParameterizedTestCaseInfo() = default;

  // Returns the name of the test case.
  const fbl::String& name() const { return name_; }

  // Registers all parametrized tests of this test case with |runner|.
  virtual void RegisterTest(Runner* runner) = 0;

  // Returns a unique Id representing the first fixture used to instantiate this Parameterized test
  // case.
  virtual TypeId GetFixtureId() const = 0;

 private:
  fbl::String name_;
};

class AddTestDelegate {
 public:
  virtual ~AddTestDelegate() = default;
  virtual std::unique_ptr<ParameterizedTestCaseInfo> CreateSuite(
      const cpp17::string_view& suite_name) = 0;
  virtual bool AddTest(ParameterizedTestCaseInfo* base, const cpp17::string_view& test_name,
                       const SourceLocation& location) = 0;
};

template <typename ParamType>
class AddInstantiationDelegate {
 public:
  virtual ~AddInstantiationDelegate<ParamType>() = default;
  virtual bool AddInstantiation(
      ParameterizedTestCaseInfo* base, const fbl::String& instantiation_name,
      const SourceLocation& location, zxtest::internal::ValueProvider<ParamType>& provider,
      std::function<std::string(zxtest::TestParamInfo<ParamType>)> name_fn) = 0;
};

}  // namespace internal
}  // namespace zxtest

#endif  // ZXTEST_BASE_PARAMETERIZED_VALUE_H_
