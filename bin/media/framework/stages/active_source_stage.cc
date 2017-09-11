// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/framework/stages/active_source_stage.h"

namespace media {

ActiveSourceStageImpl::ActiveSourceStageImpl(
    std::shared_ptr<ActiveSource> source)
    : output_(this, 0), source_(source), prepared_(false) {
  FXL_DCHECK(source_);
}

ActiveSourceStageImpl::~ActiveSourceStageImpl() {}

size_t ActiveSourceStageImpl::input_count() const {
  return 0;
};

Input& ActiveSourceStageImpl::input(size_t index) {
  FXL_CHECK(false) << "input requested from source";
  abort();
}

size_t ActiveSourceStageImpl::output_count() const {
  return 1;
}

Output& ActiveSourceStageImpl::output(size_t index) {
  FXL_DCHECK(index == 0u);
  return output_;
}

PayloadAllocator* ActiveSourceStageImpl::PrepareInput(size_t index) {
  FXL_CHECK(false) << "PrepareInput called on source";
  return nullptr;
}

void ActiveSourceStageImpl::PrepareOutput(size_t index,
                                          PayloadAllocator* allocator,
                                          const UpstreamCallback& callback) {
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

void ActiveSourceStageImpl::UnprepareOutput(size_t index,
                                            const UpstreamCallback& callback) {
  FXL_DCHECK(index == 0u);
  FXL_DCHECK(source_);

  source_->set_allocator(nullptr);
  output_.SetCopyAllocator(nullptr);
}

fxl::RefPtr<fxl::TaskRunner> ActiveSourceStageImpl::GetNodeTaskRunner() {
  return source_->GetTaskRunner();
}

void ActiveSourceStageImpl::Update() {
  Demand demand = output_.demand();

  {
    fxl::MutexLocker locker(&mutex_);
    if (demand != Demand::kNegative && !packets_.empty()) {
      output_.SupplyPacket(std::move(packets_.front()));
      packets_.pop_front();
      demand = Demand::kNegative;
    }
  }

  source_->SetDownstreamDemand(demand);
}

void ActiveSourceStageImpl::FlushInput(size_t index,
                                       bool hold_frame,
                                       const DownstreamCallback& callback) {
  FXL_CHECK(false) << "FlushInput called on source";
}

void ActiveSourceStageImpl::FlushOutput(size_t index) {
  FXL_DCHECK(source_);
  source_->Flush();
  fxl::MutexLocker locker(&mutex_);
  packets_.clear();
}

void ActiveSourceStageImpl::SetTaskRunner(
    fxl::RefPtr<fxl::TaskRunner> task_runner) {
  StageImpl::SetTaskRunner(task_runner);
}

void ActiveSourceStageImpl::PostTask(const fxl::Closure& task) {
  StageImpl::PostTask(task);
}

void ActiveSourceStageImpl::SupplyPacket(PacketPtr packet) {
  bool needs_update = false;

  {
    fxl::MutexLocker locker(&mutex_);
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

}  // namespace media
