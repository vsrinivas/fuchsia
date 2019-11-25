// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/thread_controller.h"

#include "src/developer/debug/zxdb/client/inline_thread_controller_test.h"
#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/stack.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/symbols/function.h"

namespace zxdb {

namespace {

// Provide an implementation of the ThreadController's pure virtual functions so we can instantiate
// it.
class DummyThreadController : public ThreadController {
 public:
  DummyThreadController() = default;
  ~DummyThreadController() = default;

  // ThreadController implementation.
  void InitWithThread(Thread* thread, fit::callback<void(const Err&)> cb) override {
    SetThread(thread);
    cb(Err());
  }
  ContinueOp GetContinueOp() override { return ContinueOp::StepInstruction(); }
  StopOp OnThreadStop(debug_ipc::ExceptionType stop_type,
                      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override {
    return kStopDone;
  }
  const char* GetName() const override { return "Dummy"; }

  // Make public for the test to use.
  using ThreadController::InlineFrameIs;
  using ThreadController::SetInlineFrameIfAmbiguous;
};

// Can't be called "ThreadControllerTest" because that's the base class for all
// thread-controller-related tests. We need the inline harness since we want to test the inline
// frame handling.
class ThreadControllerUnitTest : public InlineThreadControllerTest {};

}  // namespace

TEST_F(ThreadControllerUnitTest, SetInlineFrameIfAmbiguous) {
  // The mock stack has 6 entries, we want to test ambiguous inline frames so lop off the top two.
  // This will leave the "middle" function with its two nested inlines starting at the same address
  // as being the top.
  auto mock_frames = GetStack();
  mock_frames.erase(mock_frames.begin(), mock_frames.begin() + 2);

  SymbolContext symbol_context = mock_frames[0]->GetLocation().symbol_context();

  // Make the now-exposed top two frames have an ambiguous location (address at the beginning of
  // their code range). This isn't the case in the default test data (inline frames not at the top
  // of the stack can't be ambiguous because the physical call requires some instructions).
  uint64_t address = kMiddleInline2FunctionRange.begin();
  mock_frames[0]->SetAddress(address);
  mock_frames[0]->set_is_ambiguous_inline(true);
  mock_frames[1]->SetAddress(address);
  mock_frames[1]->set_is_ambiguous_inline(true);

  // The top two frames should have the same start address of the function range, and the same code
  // address (this is testing that the harness has set things up the way we need). The physical
  // frame below them (index 2) should also have the same code address.
  ASSERT_EQ(
      kMiddleInline2FunctionRange,
      mock_frames[0]->GetLocation().symbol().Get()->AsFunction()->GetFullRange(symbol_context));
  ASSERT_EQ(
      kMiddleInline1FunctionRange,
      mock_frames[1]->GetLocation().symbol().Get()->AsFunction()->GetFullRange(symbol_context));
  ASSERT_EQ(kMiddleInline1FunctionRange.begin(), address);
  ASSERT_EQ(kMiddleInline2FunctionRange.begin(), address);

  // Set the stack.
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);

  // Check the initial state of the inline frames on the stack. This is also pre-test validation.
  // There should be two inline frames and neither should be hidden.
  Stack& stack = thread()->GetStack();
  ASSERT_EQ(2u, stack.GetAmbiguousInlineFrameCount());
  ASSERT_EQ(0u, stack.hide_ambiguous_inline_frame_count());

  FrameFingerprint inline_2_fingerprint = stack.GetFrameFingerprint(0);
  FrameFingerprint inline_1_fingerprint = stack.GetFrameFingerprint(1);
  FrameFingerprint physical_fingerprint = stack.GetFrameFingerprint(2);

  // Supply a frame fingerprint that's not in the stack. This should be ignored.
  DummyThreadController controller;
  controller.InitWithThread(thread(), [](const Err&) {});
  controller.SetInlineFrameIfAmbiguous(DummyThreadController::InlineFrameIs::kEqual,
                                       FrameFingerprint(0x1234567, 0));
  EXPECT_EQ(2u, stack.GetAmbiguousInlineFrameCount());
  EXPECT_EQ(0u, stack.hide_ambiguous_inline_frame_count());

  // Supply the top frame fingerprint, this should also do nothing since it's
  // already the top one.
  controller.SetInlineFrameIfAmbiguous(DummyThreadController::InlineFrameIs::kEqual,
                                       inline_2_fingerprint);
  EXPECT_EQ(2u, stack.GetAmbiguousInlineFrameCount());
  EXPECT_EQ(0u, stack.hide_ambiguous_inline_frame_count());

  // Set previous to the top frame, it should hide the top frame.
  controller.SetInlineFrameIfAmbiguous(DummyThreadController::InlineFrameIs::kOneBefore,
                                       inline_2_fingerprint);
  EXPECT_EQ(1u, stack.hide_ambiguous_inline_frame_count());

  // The inline frame 1 fingerprint should hide the top inline frame.
  controller.SetInlineFrameIfAmbiguous(DummyThreadController::InlineFrameIs::kEqual,
                                       inline_1_fingerprint);
  EXPECT_EQ(2u, stack.GetAmbiguousInlineFrameCount());
  EXPECT_EQ(1u, stack.hide_ambiguous_inline_frame_count());

  // Set previous to inline frame 1, it should hide two frames.
  controller.SetInlineFrameIfAmbiguous(DummyThreadController::InlineFrameIs::kOneBefore,
                                       inline_1_fingerprint);
  EXPECT_EQ(2u, stack.hide_ambiguous_inline_frame_count());

  // Top physical frame should hide both inline frames.
  controller.SetInlineFrameIfAmbiguous(DummyThreadController::InlineFrameIs::kEqual,
                                       physical_fingerprint);
  EXPECT_EQ(2u, stack.GetAmbiguousInlineFrameCount());
  EXPECT_EQ(2u, stack.hide_ambiguous_inline_frame_count());

  // Go back to the frame 1 fingerprint. This should work even though its currently hidden.
  controller.SetInlineFrameIfAmbiguous(DummyThreadController::InlineFrameIs::kEqual,
                                       inline_1_fingerprint);
  EXPECT_EQ(1u, stack.hide_ambiguous_inline_frame_count());

  // Set previous to the top physical frame should be invalid because it's not ambiguous (there's a
  // physical frame in the way). As a result, the hide count should be unchanged from before.
  controller.SetInlineFrameIfAmbiguous(DummyThreadController::InlineFrameIs::kOneBefore,
                                       physical_fingerprint);
  EXPECT_EQ(1u, stack.hide_ambiguous_inline_frame_count());

  // Make a case that's not ambiguous because the current location isn't at the top of the beginning
  // of an inline function range.
  mock_frames = GetStack();
  mock_frames.erase(mock_frames.begin(), mock_frames.begin() + 2);
  mock_frames[0]->SetAddress(mock_frames[0]->GetAddress() + 4);
  mock_frames[0]->set_is_ambiguous_inline(false);
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);
}

}  // namespace zxdb
