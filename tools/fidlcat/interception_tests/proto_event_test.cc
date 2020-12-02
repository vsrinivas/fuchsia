// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <ctime>
#include <memory>
#include <sstream>
#include <vector>

#include <gtest/gtest.h>

#include "tools/fidlcat/interception_tests/test_library.h"
#include "tools/fidlcat/lib/event.h"
#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"
#include "tools/fidlcat/proto/session.pb.h"

namespace fidlcat {

#define kPid 1234
#define kTid 5678
#define kHandle0 0xabcd
#define kHandle1 0xbeef

class ProtoEventTest : public ::testing::Test {
 protected:
  ProtoEventTest() {
    loader_ = GetTestLibraryLoader();
    EXPECT_NE(loader_, nullptr);
    decode_options_.output_mode = OutputMode::kStandard;
    dispatcher_ =
        std::make_unique<SyscallDisplayDispatcher>(loader_, decode_options_, display_options_, ss_);
    Process* process = dispatcher_->CreateProcess("my_process.cmx", kPid, nullptr);
    dispatcher_->CreateThread(kTid, process);
  }

  fidl_codec::LibraryLoader* loader() const { return loader_; }
  SyscallDisplayDispatcher* dispatcher() const { return dispatcher_.get(); }

  std::string GetResult() {
    std::string result = ss_.str();
    ss_.str("");
    return result;
  }

 private:
  fidl_codec::LibraryLoader* loader_ = nullptr;
  DecodeOptions decode_options_;
  DisplayOptions display_options_;
  std::stringstream ss_;
  std::unique_ptr<SyscallDisplayDispatcher> dispatcher_;
};

#define TEST_PROTO_EVENT(name, event, expected)            \
  TEST_F(ProtoEventTest, name) {                           \
    int64_t timestamp = time(nullptr);                     \
    proto::Event proto_event;                              \
    (event)->Write(&proto_event);                          \
    EventDecoder decoder(dispatcher());                    \
    bool ok = decoder.DecodeAndDispatchEvent(proto_event); \
    ASSERT_TRUE(ok);                                       \
    std::string result = GetResult();                      \
    ASSERT_EQ(result, expected);                           \
  }

TEST_PROTO_EVENT(ProcessLaunchedFailed,
                 std::make_shared<ProcessLaunchedEvent>(timestamp, "run my_command",
                                                        "failed to run"),
                 "\nCan't launch run my_command : failed to run\n")

TEST_PROTO_EVENT(ProcessLaunchedOk,
                 std::make_shared<ProcessLaunchedEvent>(timestamp, "run my_command", ""),
                 "\nLaunched run my_command\n")

TEST_PROTO_EVENT(ProcessMonitoredFailed,
                 std::make_shared<ProcessMonitoredEvent>(timestamp,
                                                         dispatcher()->SearchProcess(kPid),
                                                         "got an error"),
                 "\nCan't monitor my_process.cmx koid=1234 : got an error\n")

TEST_PROTO_EVENT(ProcessMonitoredOk,
                 std::make_shared<ProcessMonitoredEvent>(timestamp,
                                                         dispatcher()->SearchProcess(kPid), ""),
                 "\nMonitoring my_process.cmx koid=1234\n")

TEST_PROTO_EVENT(StopMonitoring,
                 std::make_shared<StopMonitoringEvent>(timestamp,
                                                       dispatcher()->SearchProcess(kPid)),
                 "\nStop monitoring my_process.cmx koid=1234\n")

TEST_F(ProtoEventTest, InvokedAndOutputEvent) {
  int64_t timestamp_invoked = time(nullptr);
  Syscall* syscall = dispatcher()->SearchSyscall("zx_channel_create");
  auto invoked_event =
      std::make_shared<InvokedEvent>(timestamp_invoked, dispatcher()->SearchThread(kTid), syscall);
  invoked_event->AddInlineField(syscall->SearchInlineMember("options", /*invoked=*/true),
                                std::make_unique<fidl_codec::IntegerValue>(0, false));

  int64_t timestamp_output = time(nullptr);
  auto output_event = std::make_shared<OutputEvent>(
      timestamp_output, dispatcher()->SearchThread(kTid), syscall, ZX_OK, invoked_event);
  zx_handle_disposition_t handle_0 = {.operation = fidl_codec::kNoHandleDisposition,
                                      .handle = kHandle0,
                                      .type = ZX_OBJ_TYPE_CHANNEL,
                                      .rights = 0,
                                      .result = ZX_OK};
  zx_handle_disposition_t handle_1 = {.operation = fidl_codec::kNoHandleDisposition,
                                      .handle = kHandle1,
                                      .type = ZX_OBJ_TYPE_CHANNEL,
                                      .rights = 0,
                                      .result = ZX_OK};
  output_event->AddInlineField(
      syscall->SearchInlineMember("out0", /*invoked=*/false),
      std::make_unique<fidl_codec::HandleValue>(handle_0));
  output_event->AddInlineField(
      syscall->SearchInlineMember("out1", /*invoked=*/false),
      std::make_unique<fidl_codec::HandleValue>(handle_1));

  proto::Event proto_invoked_event;
  invoked_event->Write(&proto_invoked_event);
  proto::Event proto_output_event;
  output_event->Write(&proto_output_event);

  EventDecoder decoder(dispatcher());
  bool ok = decoder.DecodeAndDispatchEvent(proto_invoked_event);
  ASSERT_TRUE(ok);
  ok = decoder.DecodeAndDispatchEvent(proto_output_event);
  ASSERT_TRUE(ok);
  std::string result = GetResult();
  ASSERT_EQ(result,
            "\n"
            "my_process.cmx 1234:5678 zx_channel_create(options: uint32 = 0)\n"
            "  -> ZX_OK (out0: handle = Channel:0000abcd, out1: handle = Channel:0000beef)\n");
}

TEST_F(ProtoEventTest, Exception) {
  int64_t timestamp = time(nullptr);
  auto event = std::make_shared<ExceptionEvent>(timestamp, dispatcher()->SearchThread(kTid));
  event->stack_frame().emplace_back(Location("tools/fidlcat/main.cc", 10, 20, 0x123456789, "main"));
  event->stack_frame().emplace_back(
      Location("tools/fidlcat/foo.cc", 100, 2, 0xabcdef012345, "foo"));
  proto::Event proto_event;
  event->Write(&proto_event);
  EventDecoder decoder(dispatcher());
  bool ok = decoder.DecodeAndDispatchEvent(proto_event);
  ASSERT_TRUE(ok);
  std::string result = GetResult();
  ASSERT_EQ(result,
            "\n"
            "my_process.cmx 1234:5678 at tools/fidlcat/main.cc:10:20 main\n"
            "my_process.cmx 1234:5678 at tools/fidlcat/foo.cc:100:2 foo\n"
            "my_process.cmx 1234:5678 thread stopped on exception\n");
}

}  // namespace fidlcat
