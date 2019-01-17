// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/stack.h"

#include <map>

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/frame_fingerprint.h"
#include "garnet/bin/zxdb/expr/expr_eval_context.h"
#include "garnet/bin/zxdb/symbols/function.h"
#include "garnet/lib/debug_ipc/records.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace zxdb {

namespace {

// Implementation of Frame for inlined frames. Inlined frames have a different
// location in the source code, but refer to the underlying physical frame for
// most data.
class InlineFrame final : public Frame {
 public:
  // The physical_frame must outlive this class. Normally both are owned by the
  // Stack and have the same lifetime.
  InlineFrame(Frame* physical_frame, Location loc)
      : Frame(physical_frame->session()),
        physical_frame_(physical_frame),
        location_(loc) {}
  ~InlineFrame() override = default;

  // Frame implementation.
  Thread* GetThread() const override { return physical_frame_->GetThread(); }
  bool IsInline() const override { return true; }
  const Frame* GetPhysicalFrame() const override { return physical_frame_; }
  const Location& GetLocation() const override { return location_; }
  uint64_t GetAddress() const override { return location_.address(); }
  uint64_t GetBasePointerRegister() const override {
    return physical_frame_->GetBasePointerRegister();
  }
  std::optional<uint64_t> GetBasePointer() const override {
    return physical_frame_->GetBasePointer();
  }
  void GetBasePointerAsync(std::function<void(uint64_t bp)> cb) override {
    return physical_frame_->GetBasePointerAsync(std::move(cb));
  }
  uint64_t GetStackPointer() const override {
    return physical_frame_->GetStackPointer();
  }
  fxl::RefPtr<SymbolDataProvider> GetSymbolDataProvider() const override {
    return physical_frame_->GetSymbolDataProvider();
  }
  fxl::RefPtr<ExprEvalContext> GetExprEvalContext() const override {
    return physical_frame_->GetExprEvalContext();
  }

 private:
  Frame* physical_frame_;  // Non-owning.
  Location location_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InlineFrame);
};

// Returns a fixed-up location referring to an indexed element in an inlined
// function call chain. This also handles the case where there are no inline
// calls and the function is the only one (this returns the same location).
//
// The main_location is the location returned by symbol lookup for the
// current address.
Location LocationForInlineFrameChain(
    const std::vector<const Function*>& inline_chain, size_t chain_index,
    const Location& main_location) {
  // The file/line is the call location of the next (into the future) inlined
  // function. Fall back on the file/line from the main lookup.
  const FileLine* new_line = &main_location.file_line();
  int new_column = main_location.column();
  if (chain_index > 0) {
    const Function* next_call = inline_chain[chain_index - 1];
    if (next_call->call_line().is_valid()) {
      new_line = &next_call->call_line();
      new_column = 0;  // DWARF doesn't contain inline call column.
    }
  }

  return Location(main_location.address(), *new_line, new_column,
                  main_location.symbol_context(),
                  LazySymbol(inline_chain[chain_index]));
}

}  // namespace

Stack::Stack(Delegate* delegate) : delegate_(delegate) {}

Stack::~Stack() = default;

FrameFingerprint Stack::GetFrameFingerprint(size_t frame_index) const {
  // See function comment in thread.h for more. We need to look at the next
  // frame, so either we need to know we got them all or the caller wants the
  // 0th one. We should always have the top two stack entries if available,
  // so having only one means we got them all.
  FXL_DCHECK(frame_index == 0 || has_all_frames());

  // Should reference a valid index in the array.
  if (frame_index >= frames_.size()) {
    FXL_NOTREACHED();
    return FrameFingerprint();
  }

  // The frame address requires looking at the previous frame. When this is the
  // last entry, we can't do that. This returns the frame base pointer instead
  // which will at least identify the frame in some ways, and can be used to
  // see if future frames are younger.
  size_t prev_frame_index = frame_index + 1;
  if (prev_frame_index == frames_.size())
    return FrameFingerprint(frames_[frame_index]->GetStackPointer());

  // Use the previous frame's stack pointer. See frame_fingerprint.h.
  return FrameFingerprint(frames_[prev_frame_index]->GetStackPointer());
}

void Stack::SyncFrames(std::function<void()> callback) {
  delegate_->SyncFramesForStack(std::move(callback));
}

void Stack::SetFrames(debug_ipc::ThreadRecord::StackAmount amount,
                      const std::vector<debug_ipc::StackFrame>& frames) {
  frames_.clear();
  for (const debug_ipc::StackFrame& frame : frames)
    AppendFrame(frame);
  has_all_frames_ = amount == debug_ipc::ThreadRecord::StackAmount::kFull;
}

void Stack::SetFramesForTest(std::vector<std::unique_ptr<Frame>> frames,
                             bool has_all) {
  frames_ = std::move(frames);
  has_all_frames_ = has_all;
}

bool Stack::ClearFrames() {
  has_all_frames_ = false;

  if (frames_.empty())
    return false;  // Nothing to do.

  frames_.clear();
  return true;
}

void Stack::AppendFrame(const debug_ipc::StackFrame& record) {
  // This symbolizes all stack frames since the expansion of inline frames
  // depends on the symbols. Its possible some stack objects will never have
  // their frames queried which makes this duplicate work. A possible addition
  // is to just save the debug_ipc::StackFrames and only expand the inline
  // frames when the frame list is accessed.

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
  std::vector<const Function*> inline_chain = cur_func->GetInlineChain();
  if (inline_chain.back()->is_inline()) {
    // A non-inline frame was not found. The symbols are corrupt so give up
    // on inline processing and add the physical frame only.
    frames_.push_back(delegate_->MakeFrameForStack(record, inner_loc));
    return;
  }

  // Need to make the base "physical" frame first because all of the inline
  // frames refer to it.
  auto physical_frame = delegate_->MakeFrameForStack(
      record, LocationForInlineFrameChain(inline_chain, inline_chain.size() - 1,
                                          inner_loc));

  // Add all inline functions (skipping the last which is the physical frame
  // made above).
  for (size_t i = 0; i < inline_chain.size() - 1; i++) {
    frames_.push_back(std::make_unique<InlineFrame>(
        physical_frame.get(),
        LocationForInlineFrameChain(inline_chain, i, inner_loc)));
  }

  // Physical frame goes last (back in time).
  frames_.push_back(std::move(physical_frame));
}

}  // namespace zxdb
