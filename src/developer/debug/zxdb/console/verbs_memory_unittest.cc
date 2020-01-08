// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/console/mock_console.h"

namespace zxdb {

namespace {

using debug_ipc::MessageLoop;

class MemoryMockRemoteAPI : public MockRemoteAPI {
 public:
  // Return an empty AddressSpace reply.
  void AddressSpace(const debug_ipc::AddressSpaceRequest& request,
                    fit::callback<void(const Err&, debug_ipc::AddressSpaceReply)> cb) override {
    MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(cb)]() mutable { cb(Err(), debug_ipc::AddressSpaceReply()); });
  }
};

class VerbsMemoryTest : public RemoteAPITest {
 public:
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() {
    auto remote_api = std::make_unique<MemoryMockRemoteAPI>();
    memory_mock_remote_api_ = remote_api.get();
    return remote_api;
  }

  MemoryMockRemoteAPI* memory_mock_remote_api() const { return memory_mock_remote_api_; }

 private:
  MemoryMockRemoteAPI* memory_mock_remote_api_ = nullptr;  // Owned by System.
};

}  // namespace

// This tests that the stack command is hooked up. The register and memory decoding are tested by
// the analyze memory tests.
//
// TODO(brettw) convert to a ConsoleTest to remove some boilerplate.
TEST_F(VerbsMemoryTest, Stack) {
  MockConsole console(&session());

  // Error case with nothing running.
  console.ProcessInputLine("stack");
  auto event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ("\"stack\" requires a thread but there is no current thread.", event.output.AsString());

  // Inject a fake running process.
  constexpr uint64_t kProcessKoid = 1234;
  InjectProcess(kProcessKoid);
  constexpr uint64_t kThreadKoid = 5678;
  Thread* thread = InjectThread(kProcessKoid, kThreadKoid);

  // Eat the output from process attaching (this is asynchronously appended).
  loop().RunUntilNoTasks();
  console.FlushOutputEvents();

  // Error case with no stopped thread.
  console.ProcessInputLine("stack");
  event = console.GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ(
      "\"stack\" requires a suspended thread but thread 1 is Running.\nTo view "
      "and sync thread state with the remote system, type \"thread\".",
      event.output.AsString());

  // Thread needs to be stopped. Add two frames with some different registers.
  constexpr uint64_t kIP0 = 0x987654321;
  constexpr uint64_t kSP0 = 0x10000000;
  constexpr uint64_t kIP1 = kIP0 - 0x10;
  constexpr uint64_t kSP1 = kSP0 + 0x10;
  std::vector<std::unique_ptr<Frame>> frames;
  frames.push_back(std::make_unique<MockFrame>(
      &session(), thread, Location(Location::State::kSymbolized, kIP0), kSP0, 0,
      std::vector<debug_ipc::Register>{
          debug_ipc::Register(debug_ipc::RegisterID::kX64_rsp, kSP0)}));
  frames.push_back(std::make_unique<MockFrame>(
      &session(), thread, Location(Location::State::kSymbolized, kIP1), kSP1, 0,
      std::vector<debug_ipc::Register>{
          debug_ipc::Register(debug_ipc::RegisterID::kX64_rsp, kSP1),
          debug_ipc::Register(debug_ipc::RegisterID::kX64_rax, kSP0 + 0x20)}));
  InjectExceptionWithStack(kProcessKoid, kThreadKoid, debug_ipc::ExceptionType::kSingleStep,
                           std::move(frames), true);

  // Eat output from the exception.
  loop().RunUntilNoTasks();
  console.FlushOutputEvents();

  // Supply some memory.
  std::vector<uint8_t> mem_data;
  mem_data.resize(1024);
  mem_data[0] = 0xff;
  mem_data[1] = 0xee;
  memory_mock_remote_api()->AddMemory(kSP0, mem_data);

  console.ProcessInputLine("stack");

  loop().RunUntilNoTasks();

  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ(
      "   Address               Data \n"
      "0x10000000 0x000000000000eeff ◁ rsp\n"
      "0x10000008 0x0000000000000000 \n"
      "0x10000010 0x0000000000000000 ◁ frame 1 rsp\n"
      "0x10000018 0x0000000000000000 \n"
      "0x10000020 0x0000000000000000 ◁ frame 1 rax\n"
      "0x10000028 0x0000000000000000 \n"
      "0x10000030 0x0000000000000000 \n"
      "0x10000038 0x0000000000000000 \n"
      "0x10000040 0x0000000000000000 \n"
      "0x10000048 0x0000000000000000 \n"
      "0x10000050 0x0000000000000000 \n"
      "0x10000058 0x0000000000000000 \n"
      "0x10000060 0x0000000000000000 \n"
      "0x10000068 0x0000000000000000 \n"
      "0x10000070 0x0000000000000000 \n"
      "0x10000078 0x0000000000000000 \n"
      "0x10000080 0x0000000000000000 \n"
      "0x10000088 0x0000000000000000 \n"
      "0x10000090 0x0000000000000000 \n"
      "0x10000098 0x0000000000000000 \n"
      "↓ For more lines: stack -n 20 0x100000a0",
      event.output.AsString());
}

}  // namespace zxdb
