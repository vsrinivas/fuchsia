// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/frame_impl.h"

#include "gtest/gtest.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/register.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/location.h"

namespace zxdb {

using debug_ipc::RegisterID;

class FrameImplTest : public RemoteAPITest {
 public:
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() {
    auto remote_api = std::make_unique<MockRemoteAPI>();
    mock_remote_api_ = remote_api.get();
    return remote_api;
  }

  MockRemoteAPI* mock_remote_api() const { return mock_remote_api_; }

 private:
  MockRemoteAPI* mock_remote_api_ = nullptr;  // Owned by System.
};

class MockThread : public Thread, public Stack::Delegate {
 public:
  // The process and frame pointers must outlive this class.
  explicit MockThread(Process* process)
      : Thread(process->session()), process_(process), stack_(this) {}

  RegisterSet& register_contents() { return register_contents_; }

  // Thread implementation:
  Process* GetProcess() const override { return process_; }
  uint64_t GetKoid() const override { return 1234; }
  const std::string& GetName() const override { return thread_name_; }
  debug_ipc::ThreadRecord::State GetState() const override {
    return debug_ipc::ThreadRecord::State::kSuspended;
  }
  debug_ipc::ThreadRecord::BlockedReason GetBlockedReason() const override {
    return debug_ipc::ThreadRecord::BlockedReason::kNotBlocked;
  }
  void Pause(std::function<void()> on_paused) override {
    debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE,
                                                [on_paused]() { on_paused(); });
  }
  void Continue() override {}
  void ContinueWith(std::unique_ptr<ThreadController> controller,
                    std::function<void(const Err&)> on_continue) override {}
  void JumpTo(uint64_t new_address,
              std::function<void(const Err&)> cb) override {}
  void NotifyControllerDone(ThreadController* controller) override {}
  void StepInstruction() override {}
  const Stack& GetStack() const override { return stack_; }
  Stack& GetStack() override { return stack_; }
  void ReadRegisters(
      std::vector<debug_ipc::RegisterCategory::Type> cats_to_get,
      std::function<void(const Err&, const RegisterSet&)> cb) override {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE,
        [registers = register_contents_, cb]() { cb(Err(), registers); });
  }

 private:
  // Stack::Delegate implementation.
  void SyncFramesForStack(std::function<void(const Err&)> callback) override {
    FXL_NOTREACHED();  // All frames are available.
  }
  std::unique_ptr<Frame> MakeFrameForStack(const debug_ipc::StackFrame& input,
                                           Location location) override {
    FXL_NOTREACHED();  // Should not get called since we provide stack frames.
    return std::unique_ptr<Frame>();
  }
  Location GetSymbolizedLocationForStackFrame(
      const debug_ipc::StackFrame& input) override {
    return Location(Location::State::kSymbolized, input.ip);
  }

  std::string thread_name_ = "test thread";
  Process* process_;

  Stack stack_;

  RegisterSet register_contents_;
};

// Tests asynchronous evaluation and callbacks for evaluating the base pointer.
//
// This test uses the RemoteAPITest harness which normally creates ThreadImpls.
// But to get the stack frames the way they're needed, it creates its own
// thread implementation rather than relying on the ThreadImpl.
TEST_F(FrameImplTest, AsyncBasePointer) {
  // Make a process for notifying about.
  constexpr uint64_t kProcessKoid = 1234;
  Process* process = InjectProcess(kProcessKoid);

  // Provide a value for rax (DWARF reg 0) which will be used below.
  constexpr uint64_t kAddress = 0x86124309723;
  std::vector<debug_ipc::Register> frame_regs;
  frame_regs.emplace_back(RegisterID::kX64_rax, kAddress);

  const debug_ipc::StackFrame stack(0x12345678, 0x7890, frame_regs);
  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  // Set the memory pointed to by the register.
  constexpr uint64_t kMemoryValue = 0x78362419047;
  std::vector<uint8_t> mem_value;
  mem_value.resize(sizeof(kMemoryValue));
  memcpy(&mem_value[0], &kMemoryValue, sizeof(kMemoryValue));
  mock_remote_api()->AddMemory(kAddress, mem_value);

  // This describes the frame base location for the function. This encodes
  // the memory pointed to by register 0.
  const uint8_t kSelectRegRef[2] = {llvm::dwarf::DW_OP_reg0,
                                    llvm::dwarf::DW_OP_deref};
  VariableLocation frame_base(kSelectRegRef, 2);

  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_frame_base(frame_base);

  Location location(stack.ip, FileLine("file.cc", 12), 0, symbol_context,
                    LazySymbol(function));

  MockThread thread(process);
  thread.register_contents().set_arch(debug_ipc::Arch::kX64);

  std::vector<std::unique_ptr<Frame>> frames;
  frames.push_back(std::make_unique<FrameImpl>(&thread, stack, location));
  Frame* frame = frames[0].get();
  thread.GetStack().SetFramesForTest(std::move(frames), true);

  // This should not be able to complete synchronously because the memory isn't
  // available synchronously.
  auto optional_base = frame->GetBasePointer();
  EXPECT_FALSE(optional_base);

  uint64_t result_base = 0;
  frame->GetBasePointerAsync([&result_base](uint64_t value) {
    result_base = value;
    debug_ipc::MessageLoop::Current()->QuitNow();
  });

  // The base pointer should have picked up our register0 value for the base
  // pointer.
  debug_ipc::MessageLoop::Current()->Run();
  EXPECT_EQ(kMemoryValue, result_base);
}

}  // namespace zxdb
