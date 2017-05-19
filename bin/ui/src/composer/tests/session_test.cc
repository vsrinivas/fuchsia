// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/composer/tests/session_test.h"

namespace mozart {
namespace composer {
namespace test {

void SessionTest::SetUp() {
  session_ = ftl::MakeRefCounted<Session>(1, this, this);
}

// ::testing::Test virtual method.
void SessionTest::TearDown() {
  reported_errors_.clear();
  session_->TearDown();
  session_ = nullptr;
}

void SessionTest::ReportError(ftl::LogSeverity severity,
                              std::string error_string) {
// Typically, we don't want to log expected errors when running the tests.
// However, it is useful to print these errors while writing the tests.
#if 0
  switch (severity) {
    case ::ftl::LOG_INFO:
      FTL_LOG(INFO) << error_string;
      break;
    case ::ftl::LOG_WARNING:
      FTL_LOG(WARNING) << error_string;
      break;
    case ::ftl::LOG_ERROR:
      FTL_LOG(ERROR) << error_string;
      break;
    case ::ftl::LOG_FATAL:
      FTL_LOG(FATAL) << error_string;
      break;
  }
#endif
  reported_errors_.push_back(error_string);
}

LinkPtr SessionTest::CreateLink(Session* session,
                                ResourceId id,
                                const mozart2::LinkPtr& args) {
  if (!args->token) {
    session->error_reporter()->ERROR() << "Link token is null";
    return LinkPtr();
  } else {
    // TODO: emulate look-up of pre-registered token.
    FTL_LOG(WARNING) << "SessionTest::CreateLink() always succeeds";
    return ::ftl::MakeRefCounted<Link>(session, id);
  }
}

void SessionTest::OnSessionTearDown(Session* session) {}

}  // namespace test
}  // namespace composer
}  // namespace mozart
