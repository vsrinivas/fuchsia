// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/stack.h"

#include <map>

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/frame_fingerprint.h"
#include "garnet/lib/debug_ipc/records.h"
#include "lib/fxl/logging.h"

namespace zxdb {

Stack::Stack(Delegate* delegate) : delegate_(delegate) {}

Stack::~Stack() = default;

const std::vector<Frame*>& Stack::GetFrames() const {
  if (frames_cache_.empty()) {
    frames_cache_.reserve(frames_.size());
    for (const auto& cur : frames_)
      frames_cache_.push_back(cur.get());
  } else {
    FXL_DCHECK(frames_.size() == frames_cache_.size());
  }
  return frames_cache_;
}

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
  // The goal is to preserve pointer identity for frames. If a frame is the
  // same, weak pointers to it should remain valid.
  using IpSp = std::pair<uint64_t, uint64_t>;
  std::map<IpSp, std::unique_ptr<Frame>> existing;
  for (auto& cur : frames_) {
    IpSp key(cur->GetAddress(), cur->GetStackPointer());
    existing[key] = std::move(cur);
  }

  frames_.clear();
  frames_cache_.clear();
  for (size_t i = 0; i < frames.size(); i++) {
    IpSp key(frames[i].ip, frames[i].sp);
    auto found = existing.find(key);
    if (found == existing.end()) {
      // New frame we haven't seen.
      frames_.push_back(delegate_->MakeFrameForStack(frames[i]));
    } else {
      // Can re-use existing pointer.
      frames_.push_back(std::move(found->second));
      existing.erase(found);
    }
  }

  has_all_frames_ = amount == debug_ipc::ThreadRecord::StackAmount::kFull;
}

void Stack::SetFramesForTest(std::vector<std::unique_ptr<Frame>> frames,
                             bool has_all) {
  frames_ = std::move(frames);
  frames_cache_.clear();
  has_all_frames_ = has_all;
}

bool Stack::ClearFrames() {
  has_all_frames_ = false;

  if (frames_.empty())
    return false;  // Nothing to do.

  frames_.clear();
  frames_cache_.clear();
  return true;
}

}  // namespace zxdb
