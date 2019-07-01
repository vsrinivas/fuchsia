// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <utility>

#include <fbl/auto_call.h>
#include <zircon/assert.h>
#include <zxtest/base/test-case.h>

namespace zxtest {

namespace {

using internal::SetUpTestCaseFn;
using internal::TearDownTestCaseFn;
using internal::TestDriver;
using internal::TestStatus;

}  // namespace

TestCase::TestCase(const fbl::String& name, SetUpTestCaseFn set_up, TearDownTestCaseFn tear_down)
    : name_(name), set_up_(std::move(set_up)), tear_down_(std::move(tear_down)) {
  ZX_ASSERT_MSG(set_up_, "Invalid SetUpTestCaseFn");
  ZX_ASSERT_MSG(tear_down_, "Invalid TearDownTestCaseFn");
}
TestCase::TestCase(TestCase&& other) = default;
TestCase::~TestCase() = default;

size_t TestCase::TestCount() const {
  return test_infos_.size();
}

size_t TestCase::MatchingTestCount() const {
  return selected_indexes_.size();
}

void TestCase::Filter(TestCase::FilterFn filter) {
  fbl::Vector<unsigned long> filtered_indexes;
  filtered_indexes.reserve(test_infos_.size());
  for (unsigned long i = 0; i < test_infos_.size(); ++i) {
    const auto& test_info = test_infos_[i];
    if (!filter || filter(name_, test_info.name())) {
      filtered_indexes.push_back(i);
    }
  }
  selected_indexes_.swap(filtered_indexes);
}

void TestCase::Shuffle(uint32_t random_seed) {
  for (unsigned long i = 1; i < selected_indexes_.size(); ++i) {
    unsigned long j = rand_r(&random_seed) % (i + 1);
    if (j != i) {
      std::swap(selected_indexes_[i], selected_indexes_[j]);
    }
  }
}

void TestCase::UnShuffle() {
  for (unsigned long i = 0; i < selected_indexes_.size(); ++i) {
    selected_indexes_[i] = i;
  }
}

bool TestCase::RegisterTest(const fbl::String& name, const SourceLocation& location,
                            internal::TestFactory factory) {
  auto it = std::find_if(test_infos_.begin(), test_infos_.end(),
                         [&name](const TestInfo& info) { return info.name() == name; });

  // Test already registered.
  if (it != test_infos_.end()) {
    return false;
  }

  selected_indexes_.push_back(selected_indexes_.size());
  test_infos_.push_back(TestInfo(name, location, std::move(factory)));
  return true;
}

void TestCase::Run(LifecycleObserver* event_broadcaster, TestDriver* driver) {
  if (selected_indexes_.size() == 0) {
    return;
  }

  auto tear_down = fbl::MakeAutoCall([this, event_broadcaster] {
    tear_down_();
    event_broadcaster->OnTestCaseEnd(*this);
  });
  event_broadcaster->OnTestCaseStart(*this);
  set_up_();

  if (!driver->Continue()) {
    return;
  }

  for (unsigned long i = 0; i < selected_indexes_.size(); ++i) {
    const auto& test_info = test_infos_[selected_indexes_[i]];
    std::unique_ptr<Test> test = test_info.Instantiate(driver);
    event_broadcaster->OnTestStart(*this, test_info);
    test->Run();
    switch (driver->Status()) {
      case TestStatus::kPassed:
        event_broadcaster->OnTestSuccess(*this, test_info);
        break;
      case TestStatus::kSkipped:
        event_broadcaster->OnTestSkip(*this, test_info);
        break;
      case TestStatus::kFailed:
        event_broadcaster->OnTestFailure(*this, test_info);
        if (return_on_failure_) {
          return;
        }
        break;
      default:
        break;
    }
  }
}

}  // namespace zxtest
