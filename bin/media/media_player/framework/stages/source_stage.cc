// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/framework/stages/source_stage.h"

namespace media_player {

SourceStageImpl::SourceStageImpl(std::shared_ptr<Source> source)
    : output_(this, 0), source_(source), prepared_(false) {
  FXL_DCHECK(source_);
}

SourceStageImpl::~SourceStageImpl() {}

size_t SourceStageImpl::input_count() const {
  return 0;
};

Input& SourceStageImpl::input(size_t index) {
  FXL_CHECK(false) << "input requested from source";
  abort();
}

size_t SourceStageImpl::output_count() const {
  return 1;
}

Output& SourceStageImpl::output(size_t index) {
  FXL_DCHECK(index == 0u);
  return output_;
}

std::shared_ptr<PayloadAllocator> SourceStageImpl::PrepareInput(size_t index) {
  FXL_CHECK(false) << "PrepareInput called on source";
  return nullptr;
}

void SourceStageImpl::PrepareOutput(size_t index,
                                    std::shared_ptr<PayloadAllocator> allocator,
                                    UpstreamCallback callback) {
  FXL_DCHECK(index == 0u);
  FXL_DCHECK(source_);

  if (source_->can_accept_allocator()) {
    // Give the source the provided allocator or the default if non was
    // provided.
    source_->set_allocator(allocator == nullptr ? PayloadAllocator::GetDefault()
                                                : allocator);
  } else if (allocator) {
    // The source can't use the provided allocator, so the output must copy
    // packets.
    output_.SetCopyAllocator(allocator);
  }

  prepared_ = true;
}

void SourceStageImpl::UnprepareOutput(size_t index, UpstreamCallback callback) {
  FXL_DCHECK(index == 0u);
  FXL_DCHECK(source_);

  source_->set_allocator(nullptr);
  output_.SetCopyAllocator(nullptr);
}

GenericNode* SourceStageImpl::GetGenericNode() {
  return source_.get();
}

void SourceStageImpl::Update() {
  Demand demand = output_.demand();

  {
    std::lock_guard<std::mutex> locker(mutex_);
    if (demand != Demand::kNegative && !packets_.empty()) {
      output_.SupplyPacket(std::move(packets_.front()));
      packets_.pop_front();
      demand = Demand::kNegative;
    }
  }

  if (source_ != nullptr) {
    source_->SetDownstreamDemand(demand);
  }
}

void SourceStageImpl::FlushInput(size_t index,
                                 bool hold_frame,
                                 DownstreamCallback callback) {
  FXL_CHECK(false) << "FlushInput called on source";
}

void SourceStageImpl::FlushOutput(size_t index) {
  FXL_DCHECK(source_);
  source_->Flush();
  std::lock_guard<std::mutex> locker(mutex_);
  packets_.clear();
}

void SourceStageImpl::PostTask(const fxl::Closure& task) {
  StageImpl::PostTask(task);
}

void SourceStageImpl::SupplyPacket(PacketPtr packet) {
  bool needs_update = false;

  {
    std::lock_guard<std::mutex> locker(mutex_);
    bool packets_was_empty_ = packets_.empty();
    packets_.push_back(std::move(packet));
    if (packets_was_empty_ && prepared_) {
      needs_update = true;
    }
  }

  if (needs_update) {
    NeedsUpdate();
  }
}

}  // namespace media_player
