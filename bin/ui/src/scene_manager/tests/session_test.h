// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/scene_manager/engine/engine.h"
#include "apps/mozart/src/scene_manager/engine/session.h"
#include "gtest/gtest.h"
#include "lib/mtl/threading/thread.h"

namespace scene_manager {
namespace test {

class SessionTest : public ::testing::Test,
                    public scene_manager::ErrorReporter {
 public:
  // ::testing::Test virtual method.
  void SetUp() override;

  // ::testing::Test virtual method.
  void TearDown() override;

  // Subclasses should override to provide their own Engine.
  virtual std::unique_ptr<Engine> CreateEngine();

 protected:
  // Implement ErrorReporter.
  void ReportError(ftl::LogSeverity severity,
                   std::string error_string) override;

  // Apply the specified Op, and verify that it succeeds.
  bool Apply(mozart2::OpPtr op) { return session_->ApplyOp(std::move(op)); }

  template <class ResourceT>
  ftl::RefPtr<ResourceT> FindResource(mozart::ResourceId id) {
    return session_->resources()->FindResource<ResourceT>(id);
  }

  // Verify that the last reported error is as expected.  If no error is
  // expected, use nullptr as |expected_error_string|.
  void ExpectLastReportedError(const char* expected_error_string) {
    if (!expected_error_string) {
      EXPECT_TRUE(reported_errors_.empty());
    } else {
      EXPECT_EQ(reported_errors_.back(), expected_error_string);
    }
  }

  DisplayManager display_manager_;
  std::unique_ptr<Engine> engine_;
  SessionPtr session_;
  std::vector<std::string> reported_errors_;
};

class SessionThreadedTest : public SessionTest {
 public:
  // ::testing::Test virtual method.
  void SetUp() override;

  // ::testing::Test virtual method.
  void TearDown() override;

 protected:
  ftl::RefPtr<ftl::TaskRunner> TaskRunner() const;

  void PostTaskSync(ftl::Closure callback);

  void PostTask(ftl::AutoResetWaitableEvent& latch, ftl::Closure callback);

 private:
  mtl::Thread thread_;
};

}  // namespace test
}  // namespace scene_manager
