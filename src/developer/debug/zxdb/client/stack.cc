// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/stack.h"

#include <map>

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/frame_fingerprint.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/eval_context_impl.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"

namespace zxdb {

namespace {

// Implementation of Frame for inlined frames. Inlined frames have a different location in the
// source code, but refer to the underlying physical frame for most data.
class InlineFrame final : public Frame {
 public:
  // The physical_frame must outlive this class. Normally both are owned by the Stack and have the
  // same lifetime.
  InlineFrame(Frame* physical_frame, Location loc)
      : Frame(physical_frame->session()), physical_frame_(physical_frame), location_(loc) {}
  ~InlineFrame() override = default;

  // Frame implementation.
  Thread* GetThread() const override { return physical_frame_->GetThread(); }
  bool IsInline() const override { return true; }
  const Frame* GetPhysicalFrame() const override { return physical_frame_; }
  const Location& GetLocation() const override { return location_; }
  uint64_t GetAddress() const override { return location_.address(); }
  const std::vector<debug_ipc::Register>* GetRegisterCategorySync(
      debug_ipc::RegisterCategory category) const override {
    return physical_frame_->GetRegisterCategorySync(category);
  }
  void GetRegisterCategoryAsync(
      debug_ipc::RegisterCategory category,
      fit::function<void(const Err&, const std::vector<debug_ipc::Register>&)> cb) override {
    return physical_frame_->GetRegisterCategoryAsync(category, std::move(cb));
  }
  void WriteRegister(debug_ipc::RegisterID id, std::vector<uint8_t> data,
                     fit::callback<void(const Err&)> cb) override {
    return physical_frame_->WriteRegister(id, std::move(data), std::move(cb));
  }
  std::optional<uint64_t> GetBasePointer() const override {
    return physical_frame_->GetBasePointer();
  }
  void GetBasePointerAsync(fit::callback<void(uint64_t bp)> cb) override {
    return physical_frame_->GetBasePointerAsync(std::move(cb));
  }
  uint64_t GetCanonicalFrameAddress() const override {
    return physical_frame_->GetCanonicalFrameAddress();
  }
  uint64_t GetStackPointer() const override { return physical_frame_->GetStackPointer(); }
  fxl::RefPtr<SymbolDataProvider> GetSymbolDataProvider() const override {
    return physical_frame_->GetSymbolDataProvider();
  }
  fxl::RefPtr<EvalContext> GetEvalContext() const override {
    if (!symbol_eval_context_) {
      // Tolerate a null thread here because it makes testing much simpler. The EvalContext supports
      // a null ProcessSymbols for this case.
      fxl::WeakPtr<const ProcessSymbols> process_syms;
      if (Thread* thread = GetThread())
        process_syms = thread->GetProcess()->GetSymbols()->GetWeakPtr();
      symbol_eval_context_ =
          fxl::MakeRefCounted<EvalContextImpl>(process_syms, GetSymbolDataProvider(), location_);
    }
    return symbol_eval_context_;
  }
  bool IsAmbiguousInlineLocation() const override {
    const Location& loc = GetLocation();

    // Extract the inline function.
    if (!loc.symbol())
      return false;
    const Function* function = loc.symbol().Get()->AsFunction();
    if (!function)
      return false;
    if (!function->is_inline())
      return false;

    // There could be multiple code ranges for the inlined function, consider any of them as being a
    // candidate.
    for (const auto& cur : function->GetAbsoluteCodeRanges(loc.symbol_context())) {
      if (loc.address() == cur.begin())
        return true;
    }
    return false;
  }

 private:
  Frame* physical_frame_;  // Non-owning.
  Location location_;

  mutable fxl::RefPtr<EvalContextImpl> symbol_eval_context_;  // Lazy.

  FXL_DISALLOW_COPY_AND_ASSIGN(InlineFrame);
};

// Returns a fixed-up location referring to an indexed element in an inlined function call chain.
// This also handles the case where there are no inline calls and the function is the only one (this
// returns the same location).
//
// The main_location is the location returned by symbol lookup for the current address.
Location LocationForInlineFrameChain(const std::vector<fxl::RefPtr<Function>>& inline_chain,
                                     size_t chain_index, const Location& main_location) {
  // The file/line is the call location of the next (into the future) inlined function. Fall back on
  // the file/line from the main lookup.
  const FileLine* new_line = &main_location.file_line();
  int new_column = main_location.column();
  if (chain_index > 0) {
    const Function* next_call = inline_chain[chain_index - 1].get();
    if (next_call->call_line().is_valid()) {
      new_line = &next_call->call_line();
      new_column = 0;  // DWARF doesn't contain inline call column.
    }
  }

  return Location(main_location.address(), *new_line, new_column, main_location.symbol_context(),
                  inline_chain[chain_index]);
}

}  // namespace

Stack::Stack(Delegate* delegate) : delegate_(delegate), weak_factory_(this) {}

Stack::~Stack() = default;

fxl::WeakPtr<Stack> Stack::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

std::optional<size_t> Stack::IndexForFrame(const Frame* frame) const {
  for (size_t i = hide_ambiguous_inline_frame_count_; i < frames_.size(); i++) {
    if (frames_[i].get() == frame)
      return i - hide_ambiguous_inline_frame_count_;
  }
  return std::nullopt;
}

size_t Stack::InlineDepthForIndex(size_t index) const {
  FXL_DCHECK(index < frames_.size());
  for (size_t depth = 0; index + depth < frames_.size(); depth++) {
    if (!frames_[index + depth]->IsInline())
      return depth;
  }

  FXL_NOTREACHED();  // Should have found a physical frame that generated it.
  return 0;
}

FrameFingerprint Stack::GetFrameFingerprint(size_t virtual_frame_index) const {
  size_t frame_index = virtual_frame_index + hide_ambiguous_inline_frame_count_;

  // Should reference a valid index in the array.
  if (frame_index >= frames_.size()) {
    FXL_NOTREACHED();
    return FrameFingerprint();
  }

  // The inline frame count is the number of steps from the requested frame index to the current
  // physical frame.
  size_t inline_count = InlineDepthForIndex(frame_index);

  return FrameFingerprint(frames_[frame_index]->GetCanonicalFrameAddress(), inline_count);
}

size_t Stack::GetAmbiguousInlineFrameCount() const {
  // This can't be InlineDepthForIndex() because that takes an index relative to the
  // hide_ambiguous_inline_frame_count_ and this function always wants to return the same thing
  // regardless of the hide count.
  for (size_t i = 0; i < frames_.size(); i++) {
    if (!frames_[i]->IsAmbiguousInlineLocation())
      return i;
  }

  // Should always have a non-inline frame if there are any.
  FXL_DCHECK(frames_.empty());
  return 0;
}

void Stack::SetHideAmbiguousInlineFrameCount(size_t hide_count) {
  FXL_DCHECK(hide_count <= GetAmbiguousInlineFrameCount());
  hide_ambiguous_inline_frame_count_ = hide_count;
}

void Stack::SyncFrames(fit::callback<void(const Err&)> callback) {
  delegate_->SyncFramesForStack(std::move(callback));
}

void Stack::SetFrames(debug_ipc::ThreadRecord::StackAmount amount,
                      const std::vector<debug_ipc::StackFrame>& new_frames) {
  // See if the new frames are an extension of the existing frames or are a replacement.
  size_t appending_from = 0;  // First index in new_frames to append.
  for (size_t i = 0; i < frames_.size(); i++) {
    // The input will not contain any inline frames so skip over those when doing the checking.
    if (frames_[i]->IsInline())
      continue;

    if (appending_from >= new_frames.size() ||
        frames_[i]->GetAddress() != new_frames[appending_from].ip ||
        frames_[i]->GetStackPointer() != new_frames[appending_from].sp) {
      // New frames are not a superset of our existing stack, replace everything.
      hide_ambiguous_inline_frame_count_ = 0;
      frames_.clear();
      appending_from = 0;
      break;
    }

    appending_from++;
  }

  for (size_t i = appending_from; i < new_frames.size(); i++)
    AppendFrame(new_frames[i]);

  has_all_frames_ = amount == debug_ipc::ThreadRecord::StackAmount::kFull;
}

void Stack::SetFramesForTest(std::vector<std::unique_ptr<Frame>> frames, bool has_all) {
  frames_ = std::move(frames);
  has_all_frames_ = has_all;
  hide_ambiguous_inline_frame_count_ = 0;
}

bool Stack::ClearFrames() {
  has_all_frames_ = false;
  hide_ambiguous_inline_frame_count_ = 0;

  if (frames_.empty())
    return false;  // Nothing to do.

  frames_.clear();
  return true;
}

void Stack::AppendFrame(const debug_ipc::StackFrame& record) {
  // This symbolizes all stack frames since the expansion of inline frames depends on the symbols.
  // Its possible some stack objects will never have their frames queried which makes this duplicate
  // work. A possible addition is to just save the debug_ipc::StackFrames and only expand the inline
  // frames when the frame list is accessed.

  // Indicates we're adding the newest physical frame and its inlines to the frame list.
  bool is_top_physical_frame = frames_.empty();

  // The symbols will provide the location for the innermost inlined function.
  Location inner_loc = delegate_->GetSymbolizedLocationForStackFrame(record);

  const Function* cur_func = inner_loc.symbol().Get()->AsFunction();
  if (!cur_func) {
    // No function associated with this location.
    frames_.push_back(delegate_->MakeFrameForStack(record, inner_loc));
    return;
  }

  // The Location object will reference the most-specific inline function but
  // we need the whole chain.
  std::vector<fxl::RefPtr<Function>> inline_chain = cur_func->GetInlineChain();
  if (inline_chain.back()->is_inline()) {
    // A non-inline frame was not found. The symbols are corrupt so give up on inline processing and
    // add the physical frame only.
    frames_.push_back(delegate_->MakeFrameForStack(record, inner_loc));
    return;
  }

  // Need to make the base "physical" frame first because all of the inline frames refer to it.
  auto physical_frame = delegate_->MakeFrameForStack(
      record, LocationForInlineFrameChain(inline_chain, inline_chain.size() - 1, inner_loc));

  // Add inline functions (skipping the last which is the physical frame made above).
  for (size_t i = 0; i < inline_chain.size() - 1; i++) {
    auto inline_frame = std::make_unique<InlineFrame>(
        physical_frame.get(), LocationForInlineFrameChain(inline_chain, i, inner_loc));

    // Only add ambiguous inline frames when they correspond to the top physical frame of the stack.
    // The reason is that the instruction pointer of non-topmost stack frames represents the return
    // address. An ambiguous inline frame means the return address is the beginning of an inlined
    // function. This implies that the function call itself isn't actually in that inlined function.
    //
    // TODO(brettw) we may want to consider checking the address immediately before the IP for these
    // frames and using that for inline frame computation. This may make the stack make more sense
    // when a function call is the last part of an inline frame, but it also may make the line
    // numbers for these frames inconsistent with how they're displayed for non-inlined frames.
    if (is_top_physical_frame || !inline_frame->IsAmbiguousInlineLocation())
      frames_.push_back(std::move(inline_frame));
  }

  // Physical frame goes last (back in time).
  frames_.push_back(std::move(physical_frame));
}

}  // namespace zxdb
