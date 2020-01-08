// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_CONSOLE_TEST_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_CONSOLE_TEST_H_

#include <memory>

#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/console/mock_console.h"

namespace zxdb {

class Process;
class Thread;

// Test harness that sets up a RemoteAPITest (mocked target by replacing IPC) with a MockConsole
// (mocked console I/O) and a process/thread.
//
// The thread will be initially running. Often the first thing tests will want to do is call:
//
//   std::vector<std::unique_ptr<Frame>> frames;
//   frames.push_back(std::make_unique<MockFrame>(...));
//   InjectExceptionWithStack(ConsoleTest::kProcessKoid, ConsoleTest::kThreadKoid,
//                            debug_ipc::ExceptionType::kSingleStep, std::move(frames), true);
//
// Then to inject commands:
//
//   console().ProcessInputLine("do something");
//
// And to check output:
//
//    auto event = console().GetOutputEvent();
//    EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
//    EXPECT_EQ("Some output", event.output.AsString());
//
class ConsoleTest : public RemoteAPITest {
 public:
  // The IDs associated with the process/thread that are set up by default.
  static constexpr uint64_t kProcessKoid = 875123541;
  static constexpr uint64_t kThreadKoid = 19028730;

  MockConsole& console() { return *console_.get(); }

  Process* process() const { return process_; }
  Thread* thread() const { return thread_; }

  // testing::Test implementation.
  void SetUp() override;
  void TearDown() override;

 private:
  std::unique_ptr<MockConsole> console_;

  // The injected process/thread.
  Process* process_ = nullptr;
  Thread* thread_ = nullptr;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_CONSOLE_TEST_H_
