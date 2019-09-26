// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/step_into_thread_controller.h"

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/client/thread_controller_test.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"

namespace zxdb {

namespace {

// This override of MockModuleSymbols allows us to respond with different addresses depending on
// whether prologue skipping was requested or not (the normal mock doesn't provide this level of
// control).
class StepIntoMockModuleSymbols : public MockModuleSymbols {
 public:
  // IP of beginning of nested function where the prologue will be queried.
  static constexpr uint64_t kNestedBegin = ThreadControllerTest::kSymbolizedModuleAddress + 0x2000;

  // IP of first non-prologue instruction of the function above.
  static constexpr uint64_t kNestedPrologueEnd =
      ThreadControllerTest::kSymbolizedModuleAddress + 0x2010;

  // ModuleSymbols overrides.
  std::vector<Location> ResolveInputLocation(const SymbolContext& symbol_context,
                                             const InputLocation& input_location,
                                             const ResolveOptions& options) const override {
    if (input_location.type == InputLocation::Type::kAddress &&
        input_location.address == kNestedBegin) {
      // This is the address in question.
      if (options.skip_function_prologue)
        return {Location(Location::State::kSymbolized, kNestedPrologueEnd)};
      return {Location(Location::State::kSymbolized, kNestedBegin)};
    }
    return MockModuleSymbols::ResolveInputLocation(symbol_context, input_location, options);
  }

 protected:
  FRIEND_MAKE_REF_COUNTED(StepIntoMockModuleSymbols);
  FRIEND_REF_COUNTED_THREAD_SAFE(StepIntoMockModuleSymbols);

  StepIntoMockModuleSymbols() : MockModuleSymbols("file.so") {}
  ~StepIntoMockModuleSymbols() override {}
};

class StepIntoThreadControllerTest : public ThreadControllerTest {
 public:
  void DoStepTest(bool skip_prologue) {
    constexpr uint64_t kBeginAddr = kSymbolizedModuleAddress + 0x1000;
    constexpr uint64_t kEndAddr = kSymbolizedModuleAddress + 0x1010;

    constexpr uint64_t kStackFramePrevious = 0x5010;
    constexpr uint64_t kStackFrameInitial = 0x5000;
    constexpr uint64_t kStackFrameNested = 0x4090;

    // Set up the thread to be stopped at the beginning of our range.
    debug_ipc::NotifyException exception;
    exception.type = debug_ipc::ExceptionType::kSingleStep;
    exception.thread.process_koid = process()->GetKoid();
    exception.thread.thread_koid = thread()->GetKoid();
    exception.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
    exception.thread.frames.emplace_back(kBeginAddr, kStackFrameInitial, kStackFrameInitial);
    exception.thread.frames.emplace_back(kBeginAddr - 10, kStackFramePrevious, kStackFramePrevious);
    InjectException(exception);

    // Start the "step into" over that range.
    auto step_into = std::make_unique<StepIntoThreadController>(
        AddressRanges(AddressRange(kBeginAddr, kEndAddr)));
    bool continued = false;
    step_into->set_should_skip_prologue(skip_prologue);
    thread()->ContinueWith(std::move(step_into), [&continued](const Err& err) {
      if (!err.has_error())
        continued = true;
    });

    // That should have resumed the thread.
    EXPECT_TRUE(continued);
    EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

    // Stop at the beginning of a new stack frame (this adds to the previous stack frame still in th
    // exception record).
    exception.thread.frames.emplace(exception.thread.frames.begin(),
                                    StepIntoMockModuleSymbols::kNestedBegin, kStackFrameNested,
                                    kStackFrameNested);
    InjectException(exception);

    if (!skip_prologue) {
      // When not skipping prologues, the thread should stop since we're in a new frame.
      EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());
      return;
    }

    // When skipping prologues, it should continue through the prologue.
    EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

    // Report a stop at the end of the prologue. This just updates the same stack frame still in the
    // exception record.
    exception.thread.frames.front().ip = StepIntoMockModuleSymbols::kNestedPrologueEnd;
    InjectException(exception);

    // That should have stopped.
    EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());
  }

 protected:
  // ThreadControllerTest override:
  fxl::RefPtr<MockModuleSymbols> MakeModuleSymbols() override {
    return fxl::MakeRefCounted<StepIntoMockModuleSymbols>();
  }
};

}  // namespace

TEST_F(StepIntoThreadControllerTest, SkipPrologue) { DoStepTest(true); }

TEST_F(StepIntoThreadControllerTest, WithPrologue) { DoStepTest(false); }

}  // namespace zxdb
