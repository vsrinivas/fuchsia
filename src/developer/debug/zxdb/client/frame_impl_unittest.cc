// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/frame_impl.h"

#include "gtest/gtest.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/mock_thread.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/location.h"

namespace zxdb {

using debug_ipc::RegisterID;

class FrameImplTest : public RemoteAPITest {};

// Mock remote API for the register test that provides the logic for testing register sets.
class MockRemoteAPIForRegister : public MockRemoteAPI {
 public:
  // It's expected that RBX is set to this value, and these two values are returned when a register
  // is set.
  constexpr static uint8_t kRbxValue[8] = {0x3, 0x2, 0x1, 0x0, 0x9, 0x8, 0x7, 0x6};
  constexpr static uint8_t kRcxValue[8] = {0x4, 0x3, 0x2, 0x1, 0x0, 0x9, 0x8, 0x7};

  void WriteRegisters(const debug_ipc::WriteRegistersRequest& request,
                      fit::callback<void(const Err&, debug_ipc::WriteRegistersReply)> cb) {
    // Expect one set of RBX.
    EXPECT_EQ(1u, request.registers.size());
    std::vector<uint8_t> expected_rbx(std::begin(kRbxValue), std::end(kRbxValue));
    EXPECT_EQ(expected_rbx, request.registers[0].data);

    // Respond with the two registers we know.
    debug_ipc::WriteRegistersReply reply;
    reply.status = 0;
    reply.registers.emplace_back(debug_ipc::RegisterID::kX64_rbx,
                                 std::vector<uint8_t>(std::begin(kRbxValue), std::end(kRbxValue)));
    reply.registers.emplace_back(debug_ipc::RegisterID::kX64_rcx,
                                 std::vector<uint8_t>(std::begin(kRcxValue), std::end(kRcxValue)));

    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(cb), reply]() mutable { cb(Err(), reply); });
  }
};

class FrameImplRegisterTest : public RemoteAPITest {
 protected:
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() override {
    return std::make_unique<MockRemoteAPIForRegister>();
  }
};

// Tests asynchronous evaluation and callbacks for evaluating the base pointer.
//
// This test uses the RemoteAPITest harness which normally creates ThreadImpls.  But to get the
// stack frames the way they're needed, it creates its own thread implementation rather than relying
// on the ThreadImpl.
TEST_F(FrameImplTest, AsyncBasePointer) {
  // Make a process for notifying about.
  constexpr uint64_t kProcessKoid = 1234;
  Process* process = InjectProcess(kProcessKoid);

  // Provide a value for rax (DWARF reg 0) which will be used below.
  constexpr uint64_t kAddress = 0x86124309723;
  std::vector<debug_ipc::Register> frame_regs;
  frame_regs.emplace_back(RegisterID::kX64_rax, kAddress);

  const debug_ipc::StackFrame stack(0x12345678, 0x7890, 0, frame_regs);
  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  // Set the memory pointed to by the register.
  constexpr uint64_t kMemoryValue = 0x78362419047;
  std::vector<uint8_t> mem_value;
  mem_value.resize(sizeof(kMemoryValue));
  memcpy(&mem_value[0], &kMemoryValue, sizeof(kMemoryValue));
  mock_remote_api()->AddMemory(kAddress, mem_value);

  // This describes the frame base location for the function. This encodes the memory pointed to by
  // register 0.
  const uint8_t kSelectRegRef[2] = {llvm::dwarf::DW_OP_reg0, llvm::dwarf::DW_OP_deref};
  VariableLocation frame_base(kSelectRegRef, 2);

  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_frame_base(frame_base);

  Location location(stack.ip, FileLine("file.cc", 12), 0, symbol_context, function);

  MockThread thread(process);

  std::vector<std::unique_ptr<Frame>> frames;
  frames.push_back(std::make_unique<FrameImpl>(&thread, stack, location));
  Frame* frame = frames[0].get();
  thread.GetStack().SetFramesForTest(std::move(frames), true);

  // This should not be able to complete synchronously because the memory isn't available
  // synchronously.
  auto optional_base = frame->GetBasePointer();
  EXPECT_FALSE(optional_base);

  uint64_t result_base = 0;
  bool called = false;
  frame->GetBasePointerAsync([&result_base, &called](uint64_t value) {
    result_base = value;
    called = true;
  });

  // The base pointer should have picked up our register0 value for the base pointer.
  debug_ipc::MessageLoop::Current()->RunUntilNoTasks();
  ASSERT_TRUE(called);
  EXPECT_EQ(kMemoryValue, result_base);
}

// Tests the function to set a register. It should call the backend with the new value, and then
// update its cache on success to the new value(s) sent from the agent.
TEST_F(FrameImplRegisterTest, UpdateRegister) {
  // Make a process for notifying about.
  constexpr uint64_t kProcessKoid = 1234;
  InjectProcess(kProcessKoid);
  constexpr uint64_t kThreadKoid = 5678;
  Thread* thread = InjectThread(kProcessKoid, kThreadKoid);

  // Notify of thread stop.
  debug_ipc::NotifyException break_notification;
  break_notification.type = debug_ipc::ExceptionType::kSoftware;
  break_notification.thread.process_koid = kProcessKoid;
  break_notification.thread.thread_koid = kThreadKoid;
  break_notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  break_notification.thread.frames.emplace_back(0x1234, 0x1000);
  InjectException(break_notification);

  Stack& stack = thread->GetStack();
  Frame* frame = stack[0];

  std::vector<uint8_t> rbx_value(std::begin(MockRemoteAPIForRegister::kRbxValue),
                                 std::end(MockRemoteAPIForRegister::kRbxValue));
  std::vector<uint8_t> rcx_value(std::begin(MockRemoteAPIForRegister::kRcxValue),
                                 std::end(MockRemoteAPIForRegister::kRcxValue));

  bool called = false;
  frame->WriteRegister(debug_ipc::RegisterID::kX64_rbx, rbx_value, [&called](const Err& err) {
    EXPECT_TRUE(err.ok());
    called = true;
  });

  debug_ipc::MessageLoop::Current()->RunUntilNoTasks();
  ASSERT_TRUE(called);

  // The new values should be available for synchronous calling.
  const std::vector<debug_ipc::Register>* out_regs =
      frame->GetRegisterCategorySync(debug_ipc::RegisterCategory::kGeneral);
  ASSERT_TRUE(out_regs);

  // The two values the mock RemoteAPI put there should be returned.
  EXPECT_EQ(debug_ipc::RegisterID::kX64_rbx, (*out_regs)[0].id);
  EXPECT_EQ(rbx_value, (*out_regs)[0].data);
  EXPECT_EQ(debug_ipc::RegisterID::kX64_rcx, (*out_regs)[1].id);
  EXPECT_EQ(rcx_value, (*out_regs)[1].data);
}

}  // namespace zxdb
