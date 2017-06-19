// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/scene/session/session.h"
#include "apps/mozart/src/scene/session/session_context.h"
#include "gtest/gtest.h"
#include "lib/mtl/threading/thread.h"

namespace mozart {
namespace scene {
namespace test {

class SessionContextForTest : public SessionContext {
 public:
  SessionContextForTest(std::unique_ptr<ReleaseFenceSignaller> r);
};

class SessionTest : public ::testing::Test, public scene::ErrorReporter {
 public:
  // ::testing::Test virtual method.
  void SetUp() override;

  // ::testing::Test virtual method.
  void TearDown() override;

  // Subclasses should override to provide their own SessionContext.
  virtual std::unique_ptr<SessionContext> CreateSessionContext();

 protected:
  // Implement ErrorReporter.
  void ReportError(ftl::LogSeverity severity,
                   std::string error_string) override;

  // Apply the specified Op, and verify that it succeeds.
  bool Apply(mozart2::OpPtr op) { return session_->ApplyOp(std::move(op)); }

  template <class ResourceT>
  ftl::RefPtr<ResourceT> FindResource(ResourceId id) {
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

  std::unique_ptr<SessionContext> session_context_;
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
}  // namespace scene
}  // namespace mozart
