// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_E2E_TESTS_E2E_TEST_H_
#define SRC_DEVELOPER_DEBUG_E2E_TESTS_E2E_TEST_H_

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/console/mock_console.h"

namespace zxdb {

class E2eTest : public TestWithLoop,
                public ProcessObserver,
                public ThreadObserver,
                public BreakpointObserver {
 public:
  E2eTest();
  ~E2eTest() override;

  void ConfigureSymbolsWithFile(std::string_view symbol_file_path);

  Session& session() const { return *session_; }
  MockConsole& console() const { return *console_; }

 private:
  Err ConnectToDebugAgent();

  std::unique_ptr<Session> session_;
  std::unique_ptr<MockConsole> console_;

  std::string socket_path_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_E2E_TESTS_E2E_TEST_H_
