// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/framework/stages/sink_stage.h"

namespace media_player {

SinkStageImpl::SinkStageImpl(std::shared_ptr<Sink> sink)
    : input_(this, 0), sink_(sink) {
  FXL_DCHECK(sink_);
  sink_demand_ = Demand::kNegative;
}

SinkStageImpl::~SinkStageImpl() {}

size_t SinkStageImpl::input_count() const { return 1; };

Input& SinkStageImpl::input(size_t index) {
  FXL_DCHECK(index == 0u);
  return input_;
}

size_t SinkStageImpl::output_count() const { return 0; }

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

GenericNode* SinkStageImpl::GetGenericNode() { return sink_.get(); }

void SinkStageImpl::Update() {
  FXL_DCHECK(sink_);

  if (input_.packet()) {
    Demand demand = sink_->SupplyPacket(input_.TakePacket(Demand::kNegative));
    if (demand != Demand::kNegative) {
      // |sink_demand_| may already be |kPositive| or |kNeutral| due to a call
      // to |SetDemand|, in which case this assignment is redundant.
      sink_demand_ = demand;
    }
  }

  Demand expected = Demand::kPositive;
  if (sink_demand_.compare_exchange_strong(expected, Demand::kNegative)) {
    // |sink_demand_| was |kPositive|, and now we've reset it to |kNegative|.
    // Set demand on the input to |kPositive|.
    input_.SetDemand(expected);
    return;
  }

  expected = Demand::kNeutral;
  if (sink_demand_.compare_exchange_strong(expected, Demand::kNegative)) {
    // |sink_demand_| was |kNeutral|, and now we've reset it to |kNegative|.
    // Set demand on the input to |kNeutral|.
    input_.SetDemand(expected);
  }
}

void SinkStageImpl::FlushInput(size_t index, bool hold_frame,
                               fxl::Closure callback) {
  FXL_DCHECK(index == 0u);
  FXL_DCHECK(sink_);
  input_.Flush();
  sink_->Flush(hold_frame);
  sink_demand_ = Demand::kNegative;
  callback();
}

void SinkStageImpl::FlushOutput(size_t index, fxl::Closure callback) {
  FXL_CHECK(false) << "FlushOutput called on sink";
}

void SinkStageImpl::PostTask(const fxl::Closure& task) {
  StageImpl::PostTask(task);
}

void SinkStageImpl::SetDemand(Demand demand) {
  FXL_DCHECK(demand != Demand::kNegative);

  Demand expected = Demand::kNegative;
  if (sink_demand_.compare_exchange_strong(expected, demand)) {
    // We've signalled demand by setting |sink_demand_|, which gets reset to
    // |kNegative| in |Update| when the new demeand is communicated to the
    // input.
    NeedsUpdate();
  }
}

}  // namespace media_player
