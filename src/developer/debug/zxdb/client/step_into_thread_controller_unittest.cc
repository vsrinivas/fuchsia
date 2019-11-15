// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/step_into_thread_controller.h"

#include "src/developer/debug/zxdb/client/inline_thread_controller_test.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/developer/debug/zxdb/symbols/line_details.h"

namespace zxdb {

namespace {

// This override of MockModuleSymbols allows us to respond with different addresses depending on
// whether prologue skipping was requested or not (the normal mock doesn't provide this level of
// control).
class StepIntoMockModuleSymbols : public MockModuleSymbols {
 public:
  // IP of beginning of function where the prologue will be queried.
  static const uint64_t kNestedBegin;
  // IP of first non-prologue instruction of the function above.
  static const uint64_t kNestedPrologueEnd;

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

  StepIntoMockModuleSymbols() : MockModuleSymbols("file.so") {
    AddLineDetails(
        InlineThreadControllerTest::kTopInlineFunctionRange.begin(),
        LineDetails(InlineThreadControllerTest::kTopInlineFileLine,
                    {LineDetails::LineEntry(InlineThreadControllerTest::kTopInlineFunctionRange)}));
  }
  ~StepIntoMockModuleSymbols() override {}
};

const uint64_t StepIntoMockModuleSymbols::kNestedBegin =
    InlineThreadControllerTest::kTopInlineFunctionRange.begin();
const uint64_t StepIntoMockModuleSymbols::kNestedPrologueEnd = kNestedBegin + 4;

class StepIntoThreadControllerTest : public InlineThreadControllerTest {
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

// Inlines should never have prologues skipped. The prologue finder has a fallback that it will
// find a prologue even if one isn't explicitly noted to handle some GCC-generated code. If called
// on an inline routine, it will skip the first line.
TEST_F(StepIntoThreadControllerTest, Inline) {
  // Recall the top frame from GetStack() is inline.
  auto mock_frames = GetStack();

  // Stepping into the 0th frame from the first. These are the source locations.
  FileLine file_line = mock_frames[1]->GetLocation().file_line();

  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);

  // Hide the inline frame at the top so we're about to step into it.
  Stack& stack = thread()->GetStack();
  stack.SetHideAmbiguousInlineFrameCount(1);

  // Do the "step into".
  auto step_into_controller = std::make_unique<StepIntoThreadController>(StepMode::kSourceLine);
  bool continued = false;
  thread()->ContinueWith(std::move(step_into_controller), [&continued](const Err& err) {
    if (!err.has_error())
      continued = true;
  });
  EXPECT_TRUE(continued);

  // That should have requested a synthetic exception which will be sent out asynchronously. The
  // Resume() call will cause the MockRemoteAPI to exit the message loop.
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());  // Nothing yet.
  loop().RunUntilNoTasks();

  // The operation should have unhidden the inline stack frame rather than actually affecting the
  // backend.
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());
  EXPECT_EQ(0u, stack.hide_ambiguous_inline_frame_count());
}

}  // namespace zxdb
