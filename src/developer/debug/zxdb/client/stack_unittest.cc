// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/stack.h"

#include <map>

#include "gtest/gtest.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/frame_fingerprint.h"
#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/client/mock_stack_delegate.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

namespace {

class StackTest : public TestWithLoop {};

// Stack pointers used by MakeInlineStackFrames.
constexpr uint64_t kTopSP = 0x2000;
constexpr uint64_t kMiddleSP = 0x2020;
constexpr uint64_t kBottomSP = 0x2040;

// Returns a set of stack frames:
//   [0] =   inline #2 from frame 2
//   [1] =   inline #1 from frame 2
//   [2] = physical frame at kTopSP
//   [3] =   inline from frame 4
//   [4] = physical frame at kMiddleSP
//   [5] = physical frame at kBottomSP
std::vector<std::unique_ptr<Frame>> MakeInlineStackFrames() {
  // Create three physical frames.
  Location top_location(Location::State::kSymbolized, 0x1000);
  Location middle_location(Location::State::kSymbolized, 0x1010);
  Location bottom_location(Location::State::kSymbolized, 0x1020);

  auto phys_top = std::make_unique<MockFrame>(nullptr, nullptr, top_location, kTopSP, kMiddleSP);
  auto phys_middle =
      std::make_unique<MockFrame>(nullptr, nullptr, middle_location, kMiddleSP, kBottomSP);
  auto phys_bottom = std::make_unique<MockFrame>(nullptr, nullptr, bottom_location, kBottomSP, 0);

  std::vector<std::unique_ptr<Frame>> frames;

  // Top frame has two inline functions expanded on top of it. This uses the same Location object
  // for simplicity, in real life these will be different.
  frames.push_back(std::make_unique<MockFrame>(nullptr, nullptr, top_location, kTopSP, kMiddleSP,
                                               std::vector<debug_ipc::Register>(), kTopSP,
                                               phys_top.get()));
  frames.push_back(std::make_unique<MockFrame>(nullptr, nullptr, top_location, kTopSP, kMiddleSP,
                                               std::vector<debug_ipc::Register>(), kTopSP,
                                               phys_top.get()));

  // Physical top frame below those.
  frames.push_back(std::move(phys_top));

  // Middle frame has one inline function expanded on top of it.
  frames.push_back(std::make_unique<MockFrame>(nullptr, nullptr, middle_location, kMiddleSP,
                                               kBottomSP, std::vector<debug_ipc::Register>(),
                                               kMiddleSP, phys_middle.get()));
  frames.push_back(std::move(phys_middle));

  // Bottom frame has no inline frame.
  frames.push_back(std::move(phys_bottom));

  return frames;
}

}  // namespace

// IndexForFrame is trivial when there's no inline frame, but when there is, the returned index
// must take this into account.
TEST_F(StackTest, IndexForFrame) {
  MockStackDelegate delegate;
  Stack stack(&delegate);
  delegate.set_stack(&stack);

  // Set some stack frames with inline frames. Nothing should start out as hidden.
  auto frames = MakeInlineStackFrames();

  // Make a function for the top stack frame. It needs this to get the ranges for the ambiguity
  // comuptation.
  auto func = fxl::MakeRefCounted<Function>(DwarfTag::kInlinedSubroutine);
  func->set_assigned_name("Inline");
  // Must start exactly at kBottomAddr for the location to be ambiguous.
  uint64_t ambiguous_address = frames[0]->GetLocation().address();
  func->set_code_ranges(AddressRanges(AddressRange(ambiguous_address, ambiguous_address + 8)));

  // Force the top frame to be ambiguous. For this it needs an inline function that starts at the
  // current address.
  Location ambig_loc(ambiguous_address, FileLine("file", 10), 0,
                     SymbolContext::ForRelativeAddresses(), func);
  auto new_top_frame = std::make_unique<MockFrame>(nullptr, nullptr, ambig_loc, kTopSP, kMiddleSP,
                                                   std::vector<debug_ipc::Register>(), kTopSP,
                                                   frames[0]->GetPhysicalFrame());
  new_top_frame->set_is_ambiguous_inline(true);
  frames[0] = std::move(new_top_frame);

  stack.SetFramesForTest(std::move(frames), true);
  ASSERT_EQ(1u, stack.GetAmbiguousInlineFrameCount());
  EXPECT_EQ(0u, stack.hide_ambiguous_inline_frame_count());

  // The operator[] and the IndexForFrame results should match.
  for (size_t i = 0; i < stack.size(); i++)
    EXPECT_EQ(i, stack.IndexForFrame(stack[i])) << i;

  // Hide some inline frames, the indices should still match.
  stack.SetHideAmbiguousInlineFrameCount(1);
  for (size_t i = 0; i < stack.size(); i++)
    EXPECT_EQ(i, stack.IndexForFrame(stack[i])) << i;
}

// Tests fingerprint computations involving inline frames.
TEST_F(StackTest, InlineFingerprint) {
  MockStackDelegate delegate;
  Stack stack(&delegate);
  delegate.set_stack(&stack);
  stack.SetFramesForTest(MakeInlineStackFrames(), true);

  // The top frames (physical and inline) have the middle frame's SP as their fingerprint, along
  // with the inline count.
  EXPECT_EQ(FrameFingerprint(kMiddleSP, 2), stack.GetFrameFingerprint(0));
  EXPECT_EQ(2u, stack.InlineDepthForIndex(0));
  EXPECT_EQ(FrameFingerprint(kMiddleSP, 1), stack.GetFrameFingerprint(1));
  EXPECT_EQ(1u, stack.InlineDepthForIndex(1));
  EXPECT_EQ(FrameFingerprint(kMiddleSP, 0), stack.GetFrameFingerprint(2));
  EXPECT_EQ(0u, stack.InlineDepthForIndex(2));

  // Middle frames have the bottom frame's SP.
  EXPECT_EQ(FrameFingerprint(kBottomSP, 1), stack.GetFrameFingerprint(3));
  EXPECT_EQ(1u, stack.InlineDepthForIndex(3));
  EXPECT_EQ(FrameFingerprint(kBottomSP, 0), stack.GetFrameFingerprint(4));
  EXPECT_EQ(0u, stack.InlineDepthForIndex(4));

  // Bottom frame reports the 0 CFA.
  EXPECT_EQ(FrameFingerprint(0, 0), stack.GetFrameFingerprint(5));
  EXPECT_EQ(0u, stack.InlineDepthForIndex(5));
}

// Tests that stack frames inside inline functions are expanded so that the inline functions have
// their own "inline" frames.
//
// This tests a bottom function calling an inline function which calls a top function. The tricky
// part is the IP of the bottom frame is actually in a different inline function (the "ambiguous"
// one) because the address in the bottom frame is immediately following the TopFunc() call and this
// happens to fall in range of an inlined function. This should be omitted from the stack.
//
//   void TopFunc() {
//     ...                          // <- top_line
//   }
//
//   // Not actually on the stack but looks like it.
//   inline void bottom_ambig_inline_func() {
//     ...                          // <- inline_exec_line
//   }
//
//   inline void bottom_inline_func() {
//     ...
//     TopFunc();                   // Non-inline
//     bottom_ambig_inline_func();  // <- inline_ambig_call_line
//   }
//
//   void bottom() {
//     ...
//     bottom_inline_func();       // <- inline_call_line
//     ...
//   }
TEST_F(StackTest, InlineExpansion) {
  constexpr uint64_t kBottomAddr = 0x127365;  // IP for bottom stack frame.
  constexpr uint64_t kTopAddr = 0x893746123;  // IP for top stack frale.

  const char kFileName[] = "file.cc";
  FileLine inline_ambig_call_line(kFileName, 5);
  FileLine inline_call_line(kFileName, 10);
  FileLine inline_exec_line(kFileName, 20);
  FileLine top_line(kFileName, 30);

  MockStackDelegate delegate;
  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  // Non-inline location for the top stack frame.
  auto top_func = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  top_func->set_assigned_name("Top");
  Location top_location(kTopAddr, top_line, 0, symbol_context, top_func);
  delegate.AddLocation(top_location);

  // Bottom stack frame has a real function, an inline function, and an ambiguous inline location
  // (at the start of an inline range).
  auto bottom_ambig_inline_func = fxl::MakeRefCounted<Function>(DwarfTag::kInlinedSubroutine);
  bottom_ambig_inline_func->set_assigned_name("Inline");
  // Must start exactly at kBottomAddr for the location to be ambiguous.
  bottom_ambig_inline_func->set_code_ranges(
      AddressRanges(AddressRange(kBottomAddr, kBottomAddr + 8)));
  bottom_ambig_inline_func->set_call_line(inline_ambig_call_line);

  auto bottom_inline_func = fxl::MakeRefCounted<Function>(DwarfTag::kInlinedSubroutine);
  bottom_inline_func->set_assigned_name("Inline");
  // Must start before at kBottomAddr for the location to not be ambiguous.
  bottom_inline_func->set_code_ranges(
      AddressRanges(AddressRange(kBottomAddr - 8, kBottomAddr + 8)));
  bottom_inline_func->set_call_line(inline_call_line);

  auto bottom_func = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  bottom_func->set_assigned_name("Bottom");
  bottom_func->set_code_ranges(AddressRanges(AddressRange(kBottomAddr - 8, kBottomAddr + 16)));

  // Our containing functions don't have references to the contained functions so we don't have to
  // worry about a reference cycle here (hence "unsafe").
  bottom_ambig_inline_func->set_containing_block(
      UncachedLazySymbol::MakeUnsafe(bottom_inline_func));
  bottom_inline_func->set_containing_block(UncachedLazySymbol::MakeUnsafe(bottom_func));

  // The location returned by the symbol function will have the file/line inside the inline
  // function.
  Location bottom_location(kBottomAddr, inline_exec_line, 0, symbol_context,
                           bottom_ambig_inline_func);
  delegate.AddLocation(bottom_location);

  Stack stack(&delegate);
  delegate.set_stack(&stack);

  // Send IPs that will map to the bottom and top addresses.
  constexpr uint64_t kTopSP = 0x100;
  constexpr uint64_t kBottomSP = 0x200;

  stack.SetFrames(debug_ipc::ThreadRecord::StackAmount::kFull,
                  {debug_ipc::StackFrame(kTopAddr, kTopSP, kBottomSP),
                   debug_ipc::StackFrame(kBottomAddr, kBottomSP, 0)});

  // This should expand to tree stack entries, the one in the middle should be the inline function
  // expanded from the "bottom".
  EXPECT_EQ(3u, stack.size());

  // Bottom stack frame should be the non-inline bottom function.
  EXPECT_FALSE(stack[2]->IsInline());
  EXPECT_EQ(stack[2], stack[2]->GetPhysicalFrame());
  EXPECT_EQ(kBottomAddr, stack[2]->GetAddress());
  Location loc = stack[2]->GetLocation();
  EXPECT_EQ(kBottomAddr, loc.address());
  EXPECT_EQ(inline_call_line, loc.file_line());
  EXPECT_EQ(bottom_func.get(), loc.symbol().Get()->AsFunction());

  // Middle stack frame should be the inline bottom function, referencing the bottom one as the
  // physical frame. The location should be the call line of the ambiguous inline function because
  // it's next, even though that function was omitted from the stack.
  EXPECT_TRUE(stack[1]->IsInline());
  EXPECT_EQ(stack[2], stack[1]->GetPhysicalFrame());
  EXPECT_EQ(kBottomAddr, stack[1]->GetAddress());
  loc = stack[1]->GetLocation();
  EXPECT_EQ(kBottomAddr, loc.address());
  EXPECT_EQ(inline_ambig_call_line, loc.file_line());
  EXPECT_EQ(bottom_inline_func.get(), loc.symbol().Get()->AsFunction());

  // The bottom_ambig_inline_func should be skipped because it's at the beginning of an inline call
  // and it's not at the top physical frame of the stack.

  // Top stack frame.
  EXPECT_FALSE(stack[0]->IsInline());
  EXPECT_EQ(stack[0], stack[0]->GetPhysicalFrame());
  EXPECT_EQ(kTopAddr, stack[0]->GetAddress());
  loc = stack[0]->GetLocation();
  EXPECT_EQ(kTopAddr, loc.address());
  EXPECT_EQ(top_line, loc.file_line());
  EXPECT_EQ(top_func.get(), loc.symbol().Get()->AsFunction());
}

TEST_F(StackTest, InlineHiding) {
  constexpr uint64_t kTopSP = 0x2000;
  constexpr uint64_t kBottomSP = 0x2020;

  // Create two physical frames.
  Location top_location(Location::State::kSymbolized, 0x1000);
  Location bottom_location(Location::State::kSymbolized, 0x1020);

  auto phys_top = std::make_unique<MockFrame>(nullptr, nullptr, top_location, kTopSP);
  auto phys_bottom = std::make_unique<MockFrame>(nullptr, nullptr, bottom_location, kBottomSP);

  std::vector<std::unique_ptr<Frame>> frames;

  // Top frame has two inline functions expanded on top of it.
  frames.push_back(std::make_unique<MockFrame>(nullptr, nullptr, top_location, kTopSP, 0,
                                               std::vector<debug_ipc::Register>(), kTopSP,
                                               phys_top.get(), true));
  frames.push_back(std::make_unique<MockFrame>(nullptr, nullptr, top_location, kTopSP, 0,
                                               std::vector<debug_ipc::Register>(), kTopSP,
                                               phys_top.get(), true));

  // Physical top frame below those.
  frames.push_back(std::move(phys_top));

  // Bottom frame has no inline frame.
  frames.push_back(std::move(phys_bottom));

  MockStackDelegate delegate;
  Stack stack(&delegate);
  delegate.set_stack(&stack);

  // With no frames, there should be no inline frames.
  EXPECT_EQ(0u, stack.GetAmbiguousInlineFrameCount());

  // Setting the frames should give the two inline ones, followed by two physical ones.
  stack.SetFramesForTest(std::move(frames), true);
  EXPECT_EQ(4u, stack.size());
  EXPECT_EQ(2u, stack.GetAmbiguousInlineFrameCount());

  // Hide both inline frames, the top frame should now be the physical one.
  stack.SetHideAmbiguousInlineFrameCount(2);
  EXPECT_EQ(2u, stack.size());
  EXPECT_EQ(2u, stack.GetAmbiguousInlineFrameCount());
}

// Appends stack items to an already existing stack via SetFrames(). The existing frames and the
// inline hide count should be unchanged.
TEST_F(StackTest, UpdateExisting) {
  MockStackDelegate delegate;
  Stack stack(&delegate);
  delegate.set_stack(&stack);

  // Make a stack with one physial frame and one inline frame above it.
  Location top_location(Location::State::kSymbolized, 0x1000);
  auto phys_top = std::make_unique<MockFrame>(nullptr, nullptr, top_location, kTopSP, 0,
                                              std::vector<debug_ipc::Register>(), kTopSP);
  auto inline_top =
      std::make_unique<MockFrame>(nullptr, nullptr, top_location, kTopSP, 0,
                                  std::vector<debug_ipc::Register>(), kTopSP, phys_top.get(), true);
  inline_top->set_is_ambiguous_inline(true);

  // Save for verification later.
  const Frame* frame0 = inline_top.get();
  const Frame* frame1 = phys_top.get();

  std::vector<std::unique_ptr<Frame>> input_frames;
  input_frames.push_back(std::move(inline_top));
  input_frames.push_back(std::move(phys_top));
  stack.SetFramesForTest(std::move(input_frames), true);

  // The ambiguous inline frame is hidden so we can check later this is preserved across updates.
  ASSERT_EQ(2u, stack.size());
  ASSERT_EQ(1u, stack.GetAmbiguousInlineFrameCount());
  stack.SetHideAmbiguousInlineFrameCount(1);

  // Synthesize a frame update. The first physical frame matches the first physical frame from
  // above. This uses the non-test update flow which should preserve the frame objects that haven't
  // changed.
  std::vector<debug_ipc::StackFrame> raw_frames;
  debug_ipc::StackFrame phys_top_record(0x1000, kTopSP, kBottomSP);
  raw_frames.push_back(phys_top_record);
  debug_ipc::StackFrame phys_bottom_record(0x1020, kBottomSP);
  raw_frames.push_back(phys_bottom_record);

  stack.SetFrames(debug_ipc::ThreadRecord::StackAmount::kFull, raw_frames);

  // The update should have left the existing top physical frame and the inline frame expanded on
  // top of it, and add the additional physical frame below it.
  EXPECT_EQ(1u, stack.GetAmbiguousInlineFrameCount());
  // Now that we checked it, reset the hidden frame count so we can see them.
  stack.SetHideAmbiguousInlineFrameCount(0);
  ASSERT_EQ(3u, stack.size());
  EXPECT_EQ(frame0, stack[0]);
  EXPECT_EQ(frame1, stack[1]);
  EXPECT_EQ(phys_bottom_record.ip, stack[2]->GetAddress());

  // Now supply a slightly different stack, it should be replaced and the hidden inline frame count
  // reset.
  stack.SetHideAmbiguousInlineFrameCount(0);  // So we can test for reset.
  raw_frames[0].sp++;                         // Modify frame.
  stack.SetFrames(debug_ipc::ThreadRecord::StackAmount::kFull, raw_frames);

  // The inline frame at the top should have gone away because we didn't provide any inline
  // information for the Stack to expand it.
  ASSERT_EQ(2u, stack.size());
  EXPECT_EQ(0u, stack.GetAmbiguousInlineFrameCount());
  EXPECT_EQ(raw_frames[0].ip, stack[0]->GetAddress());
  EXPECT_EQ(raw_frames[0].sp, stack[0]->GetStackPointer());
  EXPECT_EQ(raw_frames[1].ip, stack[1]->GetAddress());
  EXPECT_EQ(raw_frames[1].sp, stack[1]->GetStackPointer());
}

// Tests that variables in inline functions are found during evaluation.
//
// This sets up an inline frame and makes sure we can read a local variable out of it.
TEST_F(StackTest, InlineVars) {
  constexpr uint64_t kInlineAddr = 0x1002;
  constexpr uint64_t kPhysAddr = 0x1000;

  MockStackDelegate delegate;
  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  // Make inline function.
  auto inline_func = fxl::MakeRefCounted<Function>(DwarfTag::kInlinedSubroutine);
  inline_func->set_code_ranges(AddressRanges(AddressRange(kInlineAddr, kInlineAddr + 8)));

  // The inline function has a local variable ("var") that always evaluates to 3.
  VariableLocation::Entry loc_entry;
  loc_entry.expression = {llvm::dwarf::DW_OP_lit3, llvm::dwarf::DW_OP_stack_value};
  VariableLocation var_loc({loc_entry});

  auto int32_type = MakeInt32Type();
  auto inline_var =
      fxl::MakeRefCounted<Variable>(DwarfTag::kVariable, "var", LazySymbol(int32_type), var_loc);
  inline_func->set_variables({LazySymbol(inline_var)});

  // Make physical fuction.
  auto phys_func = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  phys_func->set_code_ranges(AddressRanges(AddressRange(kPhysAddr, kPhysAddr + 16)));
  inline_func->set_containing_block(UncachedLazySymbol::MakeUnsafe(phys_func));

  // Physical stack frame.
  Location phys_location(kPhysAddr, FileLine("file.cc", 200), 0, symbol_context, phys_func);
  delegate.AddLocation(phys_location);

  // Inline frame on top of that.
  Location inline_location(kInlineAddr, FileLine("file.cc", 100), 0, symbol_context, inline_func);
  delegate.AddLocation(inline_location);

  Stack stack(&delegate);
  delegate.set_stack(&stack);

  stack.SetFrames(debug_ipc::ThreadRecord::StackAmount::kFull,
                  {debug_ipc::StackFrame(kInlineAddr, kTopSP, kBottomSP)});
  ASSERT_EQ(2u, stack.size());  // Should have expanded the inline frame.

  // ACTUAL TEST.

  // Evaluate "var + 1" which should be "4" given var evaluates to 3.
  auto eval_context = stack[0]->GetEvalContext();
  bool called = false;
  EvalExpression("var + 1", eval_context, true, [&called](ErrOrValue value) mutable {
    called = true;
    EXPECT_FALSE(value.has_error());

    int64_t result = 0;
    Err e = value.value().PromoteTo64(&result);
    EXPECT_FALSE(e.has_error());
    EXPECT_EQ(4, result);
  });
  EXPECT_TRUE(called);  // Callback should have been issued.
}

}  // namespace zxdb
