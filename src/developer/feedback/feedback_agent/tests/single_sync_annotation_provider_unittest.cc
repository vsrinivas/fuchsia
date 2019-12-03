// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/annotations/single_sync_annotation_provider.h"

#include <lib/async/cpp/executor.h>

#include <memory>
#include <optional>
#include <string>

#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using fuchsia::feedback::Annotation;

class SingleAnnotationProvider : public SingleSyncAnnotationProvider {
 public:
  SingleAnnotationProvider(std::optional<std::string> return_value)
      : SingleSyncAnnotationProvider("value"), return_value_(return_value) {}
  std::optional<std::string> GetAnnotation() { return return_value_; }

 private:
  std::optional<std::string> return_value_;
};

class SingleSyncAnnotationProviderTest : public UnitTestFixture {
 public:
  SingleSyncAnnotationProviderTest() : executor_(dispatcher()) {}

 protected:
  void SetUpSingleAnnotationProvider(std::optional<std::string> return_value) {
    provider_ = std::make_unique<SingleAnnotationProvider>(return_value);
  }

  fit::result<std::vector<Annotation>> RunGetAnnotations() {
    fit::result<std::vector<Annotation>> result;
    executor_.schedule_task(provider_->GetAnnotations().then(
        [&result](fit::result<std::vector<Annotation>>& res) { result = std::move(res); }));
    RunLoopUntilIdle();
    return result;
  }

 private:
  async::Executor executor_;
  std::unique_ptr<SingleSyncAnnotationProvider> provider_;
};

TEST_F(SingleSyncAnnotationProviderTest, Check_NullOptReturned) {
  SetUpSingleAnnotationProvider(std::nullopt);
  EXPECT_EQ(RunGetAnnotations().state(), fit::result_state::error);
}

TEST_F(SingleSyncAnnotationProviderTest, Check_StringReturned) {
  SetUpSingleAnnotationProvider("value");
  EXPECT_EQ(RunGetAnnotations().state(), fit::result_state::ok);
}

}  // namespace
}  // namespace feedback
