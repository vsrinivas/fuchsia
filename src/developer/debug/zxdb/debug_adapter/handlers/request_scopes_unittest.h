// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_SCOPES_UNITTEST_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_SCOPES_UNITTEST_H_

#include <dap/protocol.h>
#include <dap/session.h>
#include <dap/types.h>
#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/debug_adapter/context_test.h"

namespace zxdb {

class RequestScopesTest : public DebugAdapterContextTest {
 public:
  void SetUp() override {
    DebugAdapterContextTest::SetUp();
    InitializeDebugging();

    process_ = InjectProcess(kProcessKoid);
    // Run client to receive process started event.
    RunClient();
    thread_ = InjectThread(kProcessKoid, kThreadKoid);
    // Run client to receive threads started event.
    RunClient();
  }

  dap::ResponseOrError<dap::StackTraceResponse> GetStackTrace(
      std::vector<std::unique_ptr<Frame>> frames) {
    // Inject exception.
    InjectExceptionWithStack(kProcessKoid, kThreadKoid, debug_ipc::ExceptionType::kSingleStep,
                             std::move(frames), true);

    // Receive thread stop event.
    RunClient();

    // Send stacktrace request from the client.
    dap::StackTraceRequest stack = {};
    stack.threadId = kThreadKoid;
    auto stack_response_fut = client().send(stack);

    // Read request and process it in server.
    context().OnStreamReadable();
    loop().RunUntilNoTasks();

    // Run client to receive response.
    RunClient();
    return stack_response_fut.get();
  }

  Thread* thread() { return thread_; }
  Process* process() { return process_; }

 private:
  Thread* thread_ = nullptr;
  Process* process_ = nullptr;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_HANDLERS_REQUEST_SCOPES_UNITTEST_H_
