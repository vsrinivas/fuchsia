// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/framework/stages/sink_stage.h"

namespace media_player {

SinkStageImpl::SinkStageImpl(std::shared_ptr<Sink> sink)
    : input_(this, 0), sink_(sink) {
  FXL_DCHECK(sink_);
}

SinkStageImpl::~SinkStageImpl() {}

size_t SinkStageImpl::input_count() const {
  return 1;
};

Input& SinkStageImpl::input(size_t index) {
  FXL_DCHECK(index == 0u);
  return input_;
}

size_t SinkStageImpl::output_count() const {
  return 0;
}

Output& SinkStageImpl::output(size_t index) {
  FXL_CHECK(false) << "output requested from sink";
  abort();
}

std::shared_ptr<PayloadAllocator> SinkStageImpl::PrepareInput(size_t index) {
  FXL_DCHECK(index == 0u);
  return sink_->allocator();
}

void SinkStageImpl::PrepareOutput(size_t index,
                                  std::shared_ptr<PayloadAllocator> allocator,
                                  UpstreamCallback callback) {
  FXL_CHECK(false) << "PrepareOutput called on sink";
}

GenericNode* SinkStageImpl::GetGenericNode() {
  return sink_.get();
}

void SinkStageImpl::Update() {
  FXL_DCHECK(sink_);

  Demand demand;

  {
    std::lock_guard<std::mutex> locker(mutex_);

    if (input_.packet()) {
      sink_demand_ = sink_->SupplyPacket(input_.TakePacket(Demand::kNegative));
    }

    demand = sink_demand_;
  }

  if (demand != Demand::kNegative) {
    input_.SetDemand(demand);
  }
}

void SinkStageImpl::FlushInput(size_t index,
                               bool hold_frame,
                               DownstreamCallback callback) {
  FXL_DCHECK(index == 0u);
  FXL_DCHECK(sink_);
  input_.Flush();
  sink_->Flush(hold_frame);
  std::lock_guard<std::mutex> locker(mutex_);
  sink_demand_ = Demand::kNegative;
}

void SinkStageImpl::FlushOutput(size_t index) {
  FXL_CHECK(false) << "FlushOutput called on sink";
}

void SinkStageImpl::PostTask(const fxl::Closure& task) {
  StageImpl::PostTask(task);
}

void SinkStageImpl::SetDemand(Demand demand) {
  bool needs_update = false;

  {
    std::lock_guard<std::mutex> locker(mutex_);
    if (sink_demand_ != demand) {
      sink_demand_ = demand;
      needs_update = true;
    }
  }

  if (needs_update) {
    // This can't be called with the mutex taken, because |Update| can be
    // called from |NeedsUpdate|.
    NeedsUpdate();
  }
}

}  // namespace media_player
