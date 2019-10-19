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
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/location.h"

namespace zxdb {

using debug_ipc::RegisterID;

class FrameImplTest : public RemoteAPITest {};

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
  frame->GetBasePointerAsync([&result_base](uint64_t value) {
    result_base = value;
    debug_ipc::MessageLoop::Current()->QuitNow();
  });

  // The base pointer should have picked up our register0 value for the base pointer.
  debug_ipc::MessageLoop::Current()->Run();
  EXPECT_EQ(kMemoryValue, result_base);
}

}  // namespace zxdb
