// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_E2E_TESTS_E2E_TEST_H_
#define SRC_DEVELOPER_DEBUG_E2E_TESTS_E2E_TEST_H_

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"

namespace zxdb {

class E2eTest : public TestWithLoop {
 public:
  E2eTest() = default;
  ~E2eTest() override = default;

  void SetUp() override;

  void TearDown() override;

  Session& session() { return *session_; }

 private:
  Err ConnectToDebugAgent();

  std::unique_ptr<Session> session_;

  std::string socket_path_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_E2E_TESTS_E2E_TEST_H_
