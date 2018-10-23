// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/frame_impl.h"
#include "garnet/bin/zxdb/client/mock_remote_api.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/register.h"
#include "garnet/bin/zxdb/client/remote_api_test.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/symbols/function.h"
#include "garnet/bin/zxdb/symbols/location.h"
#include "garnet/lib/debug_ipc/helper/platform_message_loop.h"
#include "gtest/gtest.h"
#include "llvm/BinaryFormat/Dwarf.h"

namespace zxdb {

class FrameImplTest : public RemoteAPITest {
 public:
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() {
    auto remote_api = std::make_unique<MockRemoteAPI>();
    mock_remote_api_ = remote_api.get();
    return remote_api;
  }

 private:
  MockRemoteAPI* mock_remote_api_ = nullptr;  // Owned by System.
};

class MockThread : public Thread {
 public:
  // The process and frame pointers must outlive this class.
  explicit MockThread(Process* process)
      : Thread(process->session()), process_(process) {}

  RegisterSet& register_contents() { return register_contents_; }

  // Sets the desired response for frame requests. Does not take ownership of
  // the pointers.
  void set_frames(std::vector<Frame*> f) { frames_ = std::move(f); }

  // Thread implementation:
  Process* GetProcess() const override { return process_; }
  uint64_t GetKoid() const override { return 1234; }
  const std::string& GetName() const override { return thread_name_; }
  debug_ipc::ThreadRecord::State GetState() const override {
    return debug_ipc::ThreadRecord::State::kSuspended;
  }
  void Pause() override {}
  void Continue() override {}
  void ContinueWith(std::unique_ptr<ThreadController> controller,
                    std::function<void(const Err&)> on_continue) override {}
  void NotifyControllerDone(ThreadController* controller) override {}
  void StepInstruction() override {}
  std::vector<Frame*> GetFrames() const override { return frames_; }
  bool HasAllFrames() const override { return true; }
  void SyncFrames(std::function<void()> callback) override {
    debug_ipc::MessageLoop::Current()->PostTask(callback);
  }
  FrameFingerprint GetFrameFingerprint(size_t frame_index) const override {
    return FrameFingerprint();
  }
  void GetRegisters(
      std::vector<debug_ipc::RegisterCategory::Type> cats_to_get,
      std::function<void(const Err&, const RegisterSet&)> cb) override {
    debug_ipc::MessageLoop::Current()->PostTask(
        [ registers = register_contents_, cb ]() { cb(Err(), registers); });
  }

 private:
  std::string thread_name_ = "test thread";
  Process* process_;
  std::vector<Frame*> frames_;

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

  constexpr uint64_t kIP = 0x12345678;
  constexpr uint64_t kSP = 0x7890;
  constexpr uint64_t kBP = 0xabcdef;
  debug_ipc::StackFrame stack;
  stack.ip = kIP;
  stack.bp = kBP;
  stack.sp = kSP;

  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  // This describes the frame base location for the function.
  const uint8_t kSelectReg0[1] = {llvm::dwarf::DW_OP_reg0};
  VariableLocation frame_base(kSelectReg0, 1);

  auto function = fxl::MakeRefCounted<Function>();
  function->set_frame_base(frame_base);

  Location location(kIP, FileLine("file.cc", 12), 0, symbol_context,
                    LazySymbol(function));

  MockThread thread(process);
  thread.register_contents().set_arch(debug_ipc::Arch::kX64);

  FrameImpl frame(&thread, stack, location);
  thread.set_frames({&frame});

  // This should not be able to complete synchronously because reg0 isn't
  // available synchronously.
  auto optional_base = frame.GetBasePointer();
  EXPECT_FALSE(optional_base);

  uint64_t sync_base = 0;
  frame.GetBasePointerAsync([&sync_base](uint64_t value) {
    sync_base = value;
    debug_ipc::MessageLoop::Current()->QuitNow();
  });

  // We didn't provide a "register 0" in the register reply which means the
  // DWARF expression evaluation will fail. This should then fall back to the
  // base pointer extracted by the backend.
  debug_ipc::MessageLoop::Current()->Run();
  EXPECT_EQ(kBP, sync_base);

  // Now set the registers. Need a new frame because the old computed base
  // pointer will be cached.
  FrameImpl frame2(&thread, stack, location);
  thread.set_frames({&frame2});
  auto& general_regs =
      thread.register_contents()
          .category_map()[debug_ipc::RegisterCategory::Type::kGeneral];

  // Set a value for "rax" which is register 0 on x64.
  uint64_t kReg0Value = 0x86124309723;
  debug_ipc::Register reg0_contents;
  reg0_contents.id = debug_ipc::RegisterID::kX64_rax;
  reg0_contents.data.resize(sizeof(kReg0Value));
  memcpy(&reg0_contents.data[0], &kReg0Value, sizeof(kReg0Value));

  general_regs.emplace_back(reg0_contents);

  frame2.GetBasePointerAsync([&sync_base](uint64_t value) {
    sync_base = value;
    debug_ipc::MessageLoop::Current()->QuitNow();
  });

  // The base pointer should have picked up our register0 value for the base
  // pointer.
  debug_ipc::MessageLoop::Current()->Run();
  EXPECT_EQ(kReg0Value, sync_base);
}

}  // namespace zxdb
