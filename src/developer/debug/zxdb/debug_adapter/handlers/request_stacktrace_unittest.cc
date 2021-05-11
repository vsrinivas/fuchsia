// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/handlers/request_stacktrace.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/common/scoped_temp_file.h"
#include "src/developer/debug/zxdb/debug_adapter/context_test.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"

namespace zxdb {

namespace {

using RequestStackTraceTest = DebugAdapterContextTest;

}  // namespace

TEST_F(RequestStackTraceTest, FullFrameAvailable) {
  InitializeDebugging();

  InjectProcess(kProcessKoid);
  // Run client to receive process started event.
  RunClient();
  auto thread = InjectThread(kProcessKoid, kThreadKoid);
  // Run client to receive threads started event.
  RunClient();

  // Insert mock frames
  // Top frame has a valid source location
  ScopedTempFile temp_file;
  fxl::RefPtr<Function> function1(fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram));
  function1->set_assigned_name("test_func1");
  function1->set_code_ranges(AddressRanges(AddressRange(0x10000, 0x10020)));
  auto location1 = Location(0x10010, FileLine(temp_file.name(), 23), 10,
                            SymbolContext::ForRelativeAddresses(), function1);

  // The source of this frame cannot be found and will not be reported in response.
  fxl::RefPtr<Function> function2(fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram));
  function2->set_assigned_name("test_func2");
  function2->set_code_ranges(AddressRanges(AddressRange(0x10024, 0x10060)));
  auto location2 = Location(0x10040, FileLine("not_found.cc", 55), 12,
                            SymbolContext::ForRelativeAddresses(), function2);

  std::vector<std::unique_ptr<Frame>> frames;
  frames.push_back(std::make_unique<MockFrame>(&session(), thread, location1, 0x2000));
  frames.push_back(std::make_unique<MockFrame>(&session(), thread, location2, 0x2020));
  InjectExceptionWithStack(kProcessKoid, kThreadKoid, debug_ipc::ExceptionType::kSingleStep,
                           std::move(frames), true);

  // Receive thread stopped event in client.
  RunClient();

  // Send request from the client.
  dap::StackTraceRequest request = {};
  request.threadId = kThreadKoid;
  auto response = client().send(request);

  // Read request and process it in server.
  context().OnStreamReadable();
  loop().RunUntilNoTasks();

  // Run client to receive response.
  RunClient();
  auto got = response.get();
  EXPECT_FALSE(got.error);
  EXPECT_EQ(got.response.totalFrames.value(), 2);
  EXPECT_EQ(got.response.stackFrames[0].column, location1.column());
  EXPECT_EQ(got.response.stackFrames[0].line, location1.file_line().line());
  EXPECT_EQ(got.response.stackFrames[0].name, function1->GetAssignedName());
  EXPECT_EQ(got.response.stackFrames[0].source.value().path.value(), temp_file.name());
  EXPECT_EQ(got.response.stackFrames[1].column, location2.column());
  EXPECT_EQ(got.response.stackFrames[1].line, location2.file_line().line());
  EXPECT_EQ(got.response.stackFrames[1].name, function2->GetAssignedName());
  EXPECT_FALSE(got.response.stackFrames[1].source);
}

TEST_F(RequestStackTraceTest, SyncFramesRequired) {
  InitializeDebugging();

  auto process = InjectProcess(kProcessKoid);
  // Run client to receive process started event.
  RunClient();
  InjectThread(kProcessKoid, kThreadKoid);
  // Run client to receive threads started event.
  RunClient();

  constexpr uint64_t kAddress[] = {0x10010, 0x10040, 0x9000};
  constexpr uint64_t kStack[] = {0x3000, 0x3020, 0x3050};
  constexpr size_t kStackSize = 3;

  // Set up symbol resolution for stack frames.
  auto mock_module = InjectMockModule(process);
  ScopedTempFile temp_file;
  std::vector<fxl::RefPtr<Function>> function;
  std::vector<Location> location;

  for (size_t i = 0; i < kStackSize; i++) {
    function.push_back(fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram));
    function[i]->set_assigned_name(std::string("test_func_") + std::to_string(i));
    function[i]->set_code_ranges(
        AddressRanges(AddressRange(kAddress[i] - 0x10, kAddress[i] + 0x10)));
    location.push_back(Location(kAddress[i], FileLine(temp_file.name(), 23 + i), 10 + i,
                                SymbolContext::ForRelativeAddresses(), function[i]));
    mock_module->AddSymbolLocations(kAddress[i], {location[i]});
  }

  // Notify of thread stop and push expected stack frames.
  debug_ipc::NotifyException break_notification;
  break_notification.type = debug_ipc::ExceptionType::kSoftwareBreakpoint;
  break_notification.thread.id = {.process = kProcessKoid, .thread = kThreadKoid};
  break_notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  InjectException(break_notification);

  debug_ipc::ThreadStatusReply expected_reply;
  expected_reply.record = break_notification.thread;
  expected_reply.record.stack_amount = debug_ipc::ThreadRecord::StackAmount::kFull;
  for (size_t i = 0; i < kStackSize; i++) {
    debug_ipc::StackFrame frame(kAddress[i], kStack[i]);
    expected_reply.record.frames.push_back(frame);
  }
  mock_remote_api()->set_thread_status_reply(expected_reply);

  // Receive thread stopped event in client.
  RunClient();

  // Send request from the client.
  dap::StackTraceRequest request = {};
  request.threadId = kThreadKoid;
  auto response = client().send(request);

  // Read request and process it in server.
  context().OnStreamReadable();
  loop().RunUntilNoTasks();

  // Run client to receive response.
  RunClient();
  auto got = response.get();
  EXPECT_FALSE(got.error);
  EXPECT_EQ(static_cast<size_t>(got.response.totalFrames.value()), kStackSize);
  for (size_t i = 0; i < kStackSize; i++) {
    EXPECT_EQ(got.response.stackFrames[i].column, location[i].column());
    EXPECT_EQ(got.response.stackFrames[i].line, location[i].file_line().line());
    EXPECT_EQ(got.response.stackFrames[i].name, function[i]->GetAssignedName());
    EXPECT_EQ(got.response.stackFrames[i].source.value().path.value(), temp_file.name());
  }
}

}  // namespace zxdb
